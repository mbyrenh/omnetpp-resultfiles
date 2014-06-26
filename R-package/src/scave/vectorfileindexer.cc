/*
 * Copyright (c) 2010, Andras Varga and Opensim Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Opensim Ltd. nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Andras Varga or Opensim Ltd. BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/stat.h>
#include <errno.h>
#include <sstream>
#include <ostream>
#include <stdlib.h>
#include "opp_ctype.h"
#include "platmisc.h"
#include "stringutil.h"
#include "scaveutils.h"
#include "scaveexception.h"
#include "filereader.h"
#include "linetokenizer.h"
#include "dataflowmanager.h"
#include "indexfile.h"
#include "indexedvectorfile.h"
#include "nodetyperegistry.h"
#include "vectorfileindexer.h"

USING_NAMESPACE

using namespace std;

static inline bool existsFile(const string fileName)
{
    struct stat s;
    return stat(fileName.c_str(), &s)==0;
}

static string createTempFileName(const string baseFileName)
{
    string prefix = baseFileName;
    prefix.append(".temp");
    string tmpFileName = prefix;
    int serial = 0;

    while (existsFile(tmpFileName))
        tmpFileName = opp_stringf("%s%d", prefix.c_str(), serial++);
    return tmpFileName;
}

// TODO: adjacent blocks are merged
void VectorFileIndexer::generateIndex(const char *vectorFileName, IProgressMonitor *monitor)
{
    FileReader reader(vectorFileName);
    LineTokenizer tokenizer(1024);
    VectorFileIndex index;
    index.vectorFileName = vectorFileName;

    char *line;
    char **tokens;
    int64 lineNo;
    int numTokens, numOfUnrecognizedLines = 0;
    VectorData *currentVectorRef = NULL;
    VectorData *lastVectorDecl = NULL;
    Block currentBlock;

    int64 onePercentFileSize = reader.getFileSize() / 100;
    int readPercentage = 0;

    if (monitor)
        monitor->beginTask(string("Indexing ")+vectorFileName, 110);

    try
    {

        while ((line=reader.getNextLineBufferPointer())!=NULL)
        {
            if (monitor)
            {
                if (monitor->isCanceled())
                {
                    monitor->done();
                    return;
                }
                if (onePercentFileSize > 0)
                {
                    int64 readBytes = reader.getNumReadBytes();
                    int currentPercentage = readBytes / onePercentFileSize;
                    if (currentPercentage > readPercentage)
                    {
                        monitor->worked(currentPercentage - readPercentage);
                        readPercentage = currentPercentage;
                    }
                }
            }

            tokenizer.tokenize(line, reader.getCurrentLineLength());
            numTokens = tokenizer.numTokens();
            tokens = tokenizer.tokens();
            lineNo = reader.getNumReadLines();

            if (numTokens == 0 || tokens[0][0] == '#')
                continue;
            else if ((tokens[0][0] == 'r' && strcmp(tokens[0], "run") == 0) ||
                     (tokens[0][0] == 'p' && strcmp(tokens[0], "param") == 0))
            {
                index.run.parseLine(tokens, numTokens, vectorFileName, lineNo);
            }
            else if (tokens[0][0] == 'a' && strcmp(tokens[0], "attr") == 0)
            {
                if (lastVectorDecl == NULL) // run attribute
                {
                    index.run.parseLine(tokens, numTokens, vectorFileName, lineNo);
                }
                else // vector attribute
                {
                    if (numTokens < 3)
                        throw ResultFileFormatException("vector file indexer: missing attribute name or value", vectorFileName, lineNo);
                    lastVectorDecl->attributes[tokens[1]] = tokens[2];
                }
            }
            else if (tokens[0][0] == 'v' && strcmp(tokens[0], "vector") == 0)
            {
                if (numTokens < 4)
                    throw ResultFileFormatException("vector file indexer: broken vector declaration", vectorFileName, lineNo);

                VectorData vector;
                if (!parseInt(tokens[1], vector.vectorId))
                    throw ResultFileFormatException("vector file indexer: malformed vector in vector declaration", vectorFileName, lineNo);
                vector.moduleName = tokens[2];
                vector.name = tokens[3];
                vector.columns = (numTokens < 5 || opp_isdigit(tokens[4][0]) ? "TV" : tokens[4]);
                vector.blockSize = 0;

                index.addVector(vector);
                lastVectorDecl = index.getVectorAt(index.getNumberOfVectors() - 1);
                currentVectorRef = NULL;
            }
            else if (tokens[0][0] == 'v' && strcmp(tokens[0], "version") == 0)
            {
                int version;
                if(numTokens < 2)
                    throw ResultFileFormatException("vector file indexer: missing version number", vectorFileName, lineNo);
                if(!parseInt(tokens[1], version))
                    throw ResultFileFormatException("vector file indexer: version is not a number", vectorFileName, lineNo);
                if(version > 2)
                    throw ResultFileFormatException("vector file indexer: expects version 2 or lower", vectorFileName, lineNo);
            }
            else // data line
            {
                int vectorId;
                simultime_t simTime;
                double value;
                eventnumber_t eventNum = -1;

                if (!parseInt(tokens[0], vectorId))
                {
                    numOfUnrecognizedLines++;
                    continue;
                }

                if (currentVectorRef == NULL || vectorId != currentVectorRef->vectorId)
                {
                    if (currentVectorRef != NULL)
                    {
                        currentBlock.size = (int64)(reader.getCurrentLineStartOffset() - currentBlock.startOffset);
                        if (currentBlock.size > currentVectorRef->blockSize)
                            currentVectorRef->blockSize = currentBlock.size;
                        currentVectorRef->addBlock(currentBlock);
                    }

                    currentBlock = Block();
                    currentBlock.startOffset = reader.getCurrentLineStartOffset();
                    currentVectorRef = index.getVectorById(vectorId);
                    if (currentVectorRef == NULL)
                        throw ResultFileFormatException("vector file indexer: missing vector declaration", vectorFileName, lineNo);
                }

                for (int i = 0; i < (int)currentVectorRef->columns.size(); ++i)
                {
                    char column = currentVectorRef->columns[i];
                    if (i+1 >= numTokens)
                        throw ResultFileFormatException("vector file indexer: data line too short", vectorFileName, lineNo);

                    char *token = tokens[i+1];
                    switch (column)
                    {
                    case 'T':
                        if (!parseSimtime(token, simTime))
                            throw ResultFileFormatException("vector file indexer: malformed simulation time", vectorFileName, lineNo);
                        break;
                    case 'V':
                        if (!parseDouble(token, value))
                            throw ResultFileFormatException("vector file indexer: malformed data value", vectorFileName, lineNo);
                        break;
                    case 'E':
                        if (!parseInt64(token, eventNum))
                            throw ResultFileFormatException("vector file indexer: malformed event number", vectorFileName, lineNo);
                        break;
                    }
                }

                currentBlock.collect(eventNum, simTime, value);
            }
        }

        // finish last block
        if (currentBlock.getCount() > 0)
        {
            assert(currentVectorRef != NULL);
            currentBlock.size = (int64)(reader.getFileSize() - currentBlock.startOffset);
            if (currentBlock.size > currentVectorRef->blockSize)
                currentVectorRef->blockSize = currentBlock.size;
            currentVectorRef->addBlock(currentBlock);
        }

        if (numOfUnrecognizedLines > 0)
        {
			// Commented out, because it uses printf (prohibited under R)
            // fprintf(stderr, "Found %d unrecognized lines in %s.\n", numOfUnrecognizedLines, vectorFileName);
        }
    }
    catch (exception&)
    {
        if (monitor)
            monitor->done();
        throw;
    }


    if (monitor)
    {
        if (monitor->isCanceled())
        {
            monitor->done();
            return;
        }
        if (readPercentage < 100)
            monitor->worked(100 - readPercentage);
    }

    // generate index file: first write it to a temp file then rename it to .vci;
    // we do this in order to prevent race conditions from other processes/threads
    // reading an incomplete .vci file
    string indexFileName = IndexFile::getIndexFileName(vectorFileName);
    string tempIndexFileName = createTempFileName(indexFileName);

    try
    {
        IndexFileWriter writer(tempIndexFileName.c_str());
        writer.writeAll(index);

        if (monitor)
            monitor->worked(10);

        // rename generated index file
        if (unlink(indexFileName.c_str())!=0 && errno!=ENOENT)
            throw opp_runtime_error("Cannot remove original index file `%s': %s", indexFileName.c_str(), strerror(errno));
        if (rename(tempIndexFileName.c_str(), indexFileName.c_str())!=0)
            throw opp_runtime_error("Cannot rename index file from '%s' to '%s': %s", tempIndexFileName.c_str(), indexFileName.c_str(), strerror(errno));
    }
    catch (exception&)
    {
        if (monitor)
            monitor->done();

        // if something wrong happened, we remove the temp files
        unlink(indexFileName.c_str());
        unlink(tempIndexFileName.c_str());
        throw;
    }

    if (monitor)
        monitor->done();
}

void VectorFileIndexer::rebuildVectorFile(const char *vectorFileName, IProgressMonitor *monitor)
{
    string indexFileName = IndexFile::getIndexFileName(vectorFileName);
    string tempIndexFileName = createTempFileName(indexFileName);
    string tempVectorFileName = createTempFileName(vectorFileName);

    try
    {
        // load file
        ResultFileManager resultFileManager;
        ResultFile *f = resultFileManager.loadFile(vectorFileName);
        if (!f)
            throw opp_runtime_error("Cannot load %s", vectorFileName);
        else if (f->numUnrecognizedLines>0){}
			// Commented out, because it uses printf (prohibited under R)
            // fprintf(stderr, "WARNING: %s: %d invalid/incomplete lines out of %d\n", vectorFileName, f->numUnrecognizedLines, f->numLines);

        IDList vectorIDList = resultFileManager.getAllVectors();
        if (vectorIDList.isEmpty()) {
			// Commented out, because it uses printf (prohibited under R)
            // fprintf(stderr, "WARNING: %s: no vectors found\n", vectorFileName);
            return;
        }

        const RunList &runList = resultFileManager.getRuns();
        if (runList.size() != 1)
            throw opp_runtime_error("Multiple runs found in %s", vectorFileName);
        const Run *runPtr = runList[0];

        //
        // assemble dataflow network for vectors
        //
        {
            DataflowManager dataflowManager;
            NodeTypeRegistry *registry = NodeTypeRegistry::getInstance();

            // create reader node
            NodeType *readerNodeType = registry->getNodeType("vectorfilereader");
            if (!readerNodeType)
                throw opp_runtime_error("There is no node type 'vectorfilereader' in the registry\n");
            StringMap attrs;
            attrs["filename"] = vectorFileName;
            Node *readerNode = readerNodeType->create(&dataflowManager, attrs);

            // create writer node
            NodeType *writerNodeType = registry->getNodeType("indexedvectorfilewriter");
            if (!writerNodeType)
                throw opp_runtime_error("There is no node type 'indexedvectorfilewriter' in the registry\n");
            StringMap writerAttrs;
            writerAttrs["filename"] = tempVectorFileName;
            writerAttrs["indexfilename"] = tempIndexFileName;
            writerAttrs["blocksize"] = "65536"; // TODO
            writerAttrs["fileheader"] = "# generated by scavetool";
            IndexedVectorFileWriterNode *writerNode =
                dynamic_cast<IndexedVectorFileWriterNode*>(writerNodeType->create(&dataflowManager, writerAttrs));
            if (!writerNode)
                throw opp_runtime_error("Cannot create the indexedvectorfilewriternode.");
            writerNode->setRun(runPtr->runName.c_str(), runPtr->attributes, runPtr->moduleParams);


            // create a ports
            for (int i=0; i<vectorIDList.size(); i++)
            {
                char portName[30];
                const VectorResult& vector = resultFileManager.getVector(vectorIDList.get(i));
                sprintf(portName, "%d", vector.vectorId);
                Port *outPort = readerNodeType->getPort(readerNode, portName);
                Port *inPort = writerNode->addVector(vector);
                dataflowManager.connect(outPort, inPort);
            }

            // run!
            dataflowManager.execute(monitor);

            // dataflowManager deleted here, and its destructor closes the files
        }

        // rename temp to orig
        if (unlink(indexFileName.c_str())!=0 && errno!=ENOENT)
            throw opp_runtime_error("Cannot remove original index file `%s': %s", indexFileName.c_str(), strerror(errno));
        if (unlink(vectorFileName)!=0)
            throw opp_runtime_error("Cannot remove original vector file `%s': %s", vectorFileName, strerror(errno));
        if (rename(tempVectorFileName.c_str(), vectorFileName)!=0)
            throw opp_runtime_error("Cannot move generated vector file '%s' to the original '%s': %s",
                    tempVectorFileName.c_str(), vectorFileName, strerror(errno));
        if (rename(tempIndexFileName.c_str(), indexFileName.c_str())!=0)
            throw opp_runtime_error("Cannot move generated index file from '%s' to '%s': %s", tempIndexFileName.c_str(), indexFileName.c_str(), strerror(errno));
    }
    catch (exception& e)
    {
        // cleanup temp files
        unlink(tempIndexFileName.c_str());
        if (existsFile(vectorFileName))
            unlink(tempVectorFileName.c_str());

        throw;
    }
}


