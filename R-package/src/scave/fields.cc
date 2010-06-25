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

#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "commonutil.h"
#include "stringutil.h"
#include "fields.h"

USING_NAMESPACE

using namespace std;

/*----------------------------------------
 *            ResultItemAttribute
 *----------------------------------------*/
const char * const ResultItemAttribute::TYPE  = "type";
const char * const ResultItemAttribute::ENUM = "enum";

StringVector ResultItemAttribute::getAttributeNames()
{
    StringVector names = StringVector();
    names.push_back(TYPE);
    names.push_back(ENUM);
    return names;
}

bool ResultItemAttribute::isAttributeName(const string name)
{
    return name == TYPE || name == ENUM;
}

/*----------------------------------------
 *              RunAttribute
 *----------------------------------------*/

//TODO: table to be kept consistent with ConfigVarDescription table in sectionbasedconfig.cc; use common source?
const char * const RunAttribute::INIFILE     = "inifile";
const char * const RunAttribute::CONFIGNAME  = "configname";
const char * const RunAttribute::RUNNUMBER   = "runnumber";
const char * const RunAttribute::NETWORK     = "network";
const char * const RunAttribute::EXPERIMENT  = "experiment";
const char * const RunAttribute::MEASUREMENT = "measurement";
const char * const RunAttribute::REPLICATION = "replication";
const char * const RunAttribute::DATETIME    = "datetime";
const char * const RunAttribute::PROCESSID   = "processid";
const char * const RunAttribute::RESULTDIR   = "resultdir";
const char * const RunAttribute::REPETITION  = "repetition";
const char * const RunAttribute::SEEDSET     = "seedset";
const char * const RunAttribute::ITERATIONVARS = "iterationvars";
const char * const RunAttribute::ITERATIONVARS2 = "iterationvars2";

StringVector RunAttribute::getAttributeNames()
{
    StringVector names = StringVector();
    names.push_back(INIFILE);
    names.push_back(CONFIGNAME);
    names.push_back(RUNNUMBER);
    names.push_back(NETWORK);
    names.push_back(EXPERIMENT);
    names.push_back(MEASUREMENT);
    names.push_back(REPLICATION);
    names.push_back(DATETIME);
    names.push_back(PROCESSID);
    names.push_back(RESULTDIR);
    names.push_back(REPETITION);
    names.push_back(SEEDSET);
    names.push_back(ITERATIONVARS);
    names.push_back(ITERATIONVARS2);
    return names;
}

bool RunAttribute::isAttributeName(const string name)
{
    return name == INIFILE || name == CONFIGNAME || name == RUNNUMBER || name == NETWORK ||
           name == EXPERIMENT || name == MEASUREMENT || name == REPLICATION ||
           name == DATETIME || name == PROCESSID || name == RESULTDIR ||
           name == REPETITION || name == SEEDSET || name == ITERATIONVARS ||
           name == ITERATIONVARS2;
}

/*----------------------------------------
 *              ResultItemField
 *----------------------------------------*/
const char * const ResultItemField::FILE   = "file";
const char * const ResultItemField::RUN    = "run";
const char * const ResultItemField::MODULE = "module";
const char * const ResultItemField::NAME   = "name";

ResultItemField::ResultItemField(const string fieldName)
{
    this->id = getFieldID(fieldName);
    this->name = fieldName;
}

int ResultItemField::getFieldID(const string fieldName)
{
    if (fieldName == ResultItemField::FILE) return ResultItemField::FILE_ID;
    else if (fieldName == ResultItemField::RUN) return ResultItemField::RUN_ID;
    else if (fieldName == ResultItemField::MODULE) return ResultItemField::MODULE_ID;
    else if (fieldName == ResultItemField::NAME) return ResultItemField::NAME_ID;
    else if (ResultItemAttribute::isAttributeName(fieldName)) return ResultItemField::ATTR_ID;
    else if (fieldName.find_first_of('.') != string::npos) return ResultItemField::RUN_PARAM_ID;
    else return ResultItemField::RUN_ATTR_ID;
}


/*----------------------------------------
 *              ResultItemFields
 *----------------------------------------*/

StringVector ResultItemFields::getFieldNames()
{
    StringVector names = StringVector();
    names.push_back(ResultItemField::FILE);
    names.push_back(ResultItemField::RUN);
    names.push_back(ResultItemField::MODULE);
    names.push_back(ResultItemField::NAME);
    StringVector attrNames = ResultItemAttribute::getAttributeNames();
    names.insert(names.end(), attrNames.begin(), attrNames.end());
    attrNames = RunAttribute::getAttributeNames();
    names.insert(names.end(), attrNames.begin(), attrNames.end());
    return names;
}

ResultItemFields::ResultItemFields(ResultItemField field)
{
    this->fields.push_back(field);
}

ResultItemFields::ResultItemFields(const string fieldName)
{
    this->fields.push_back(ResultItemField(fieldName));
}

ResultItemFields::ResultItemFields(const StringVector &fieldNames)
{
    for (StringVector::const_iterator fieldName = fieldNames.begin(); fieldName != fieldNames.end(); ++fieldName)
        this->fields.push_back(ResultItemField(*fieldName));
}

bool ResultItemFields::hasField(ResultItemField field) const
{
    return hasField(field.getName());
}

bool ResultItemFields::hasField(const string fieldName) const
{
    for (vector<ResultItemField>::const_iterator field = fields.begin(); field != fields.end(); ++field)
        if (field->getName() == fieldName)
            return true;
    return false;
}

ResultItemFields ResultItemFields::complement() const
{
    StringVector complementFields;
    StringVector fieldNames = getFieldNames();
    for (StringVector::const_iterator fieldName = fieldNames.begin(); fieldName != fieldNames.end(); ++fieldName)
        if (!hasField(*fieldName))
            complementFields.push_back(*fieldName);
    return ResultItemFields(complementFields);
}

bool ResultItemFields::equal(ID id1, ID id2, ResultFileManager *manager) const
{
    if (id1==-1 || id2==-1 || id1 == id2) return id1==id2;
    const ResultItem& d1 = manager->getItem(id1);
    const ResultItem& d2 = manager->getItem(id2);
    return equal(d1, d2);
}

bool ResultItemFields::equal(const ResultItem& d1, const ResultItem& d2) const
{
    for (vector<ResultItemField>::const_iterator field = fields.begin(); field != fields.end(); ++field)
        if (!field->equal(d1, d2))
            return false;
    return true;
}

bool ResultItemFields::less(ID id1, ID id2, ResultFileManager *manager) const
{
    if (id1==-1 || id2==-1) return id2!=-1; // -1 is the smallest
    if (id1 == id2) return false;
    const ResultItem& d1 = manager->getItem(id1);
    const ResultItem& d2 = manager->getItem(id2);
    return less(d1, d2);
}

bool ResultItemFields::less(const ResultItem &d1, const ResultItem &d2) const
{
    for (vector<ResultItemField>::const_iterator field = fields.begin(); field != fields.end(); ++field)
    {
        int cmp = field->compare(d1, d2);
        if (cmp)
            return cmp < 0;
    }
    return false; // ==
}
