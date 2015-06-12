/*
 * Cppcheck - A tool for static C/C++ code analysis
 * Copyright (C) 2007-2012 Daniel Marjamäki and Cppcheck team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * The above software in this distribution may have been modified by THL A29 Limited (“Tencent Modifications”).
 * All Tencent Modifications are Copyright (C) 2015 THL A29 Limited.
 */

//---------------------------------------------------------------------------
// Check for assignment / condition mismatches
//---------------------------------------------------------------------------

#include "checkassignif.h"
#include "symboldatabase.h"

//---------------------------------------------------------------------------

// Register this check class (by creating a static instance of it)
namespace {
    CheckAssignIf instance;
}


void CheckAssignIf::assignIf()
{
    const SymbolDatabase *symbolDatabase = _tokenizer->getSymbolDatabase();

    for (const Token *tok = _tokenizer->tokens(); tok; tok = tok->next()) {
        if (tok->str() != "=")
            continue;

        if (Token::Match(tok->tokAt(-2), "[;{}] %var% = %var% [&|] %num% ;")) {
            const unsigned int varid(tok->previous()->varId());
            if (varid == 0)
                continue;
            const char bitop(tok->strAt(2).at(0));

            const MathLib::bigint num = MathLib::toLongNumber(tok->strAt(3));
            if (num < 0)
                continue;

            bool islocal = false;
            const Variable *var = symbolDatabase->getVariableFromVarId(varid);
            if (var && var->isLocal())
                islocal = true;
            assignIfParseScope(tok, tok->tokAt(4), varid, islocal, bitop, num);
        }
    }
}

/** parse scopes recursively */
bool CheckAssignIf::assignIfParseScope(const Token * const assignTok,
                                       const Token * const startTok,
                                       const unsigned int varid,
                                       const bool islocal,
                                       const char bitop,
                                       const MathLib::bigint num)
{
    for (const Token *tok2 = startTok; tok2; tok2 = tok2->next()) {
        if (Token::Match(tok2, "[(,] &| %varid% [,)]", varid))
            return true;
        if (tok2->str() == "}")
            return false;
        if (!islocal && Token::Match(tok2, "%var% (") && !Token::simpleMatch(tok2->next()->link(), ") {"))
            return true;
        if (Token::Match(tok2, "if|while (")) {
            if (!islocal && tok2->str() == "while")
                continue;

            // parse condition
            const Token * const end = tok2->next()->link();
			if(!end)
				continue;
            for (; tok2 != end; tok2 = tok2->next()) {
                if (Token::Match(tok2, "[(,] &| %varid% [,)]", varid)) {
                    return true;
                }
                if (Token::Match(tok2,"&&|%oror%|( %varid% %any% %num% &&|%oror%|)", varid)) {
                    const Token *vartok = tok2->next();
                    const std::string& op(vartok->strAt(1));
                    const MathLib::bigint num2 = MathLib::toLongNumber(vartok->strAt(2));
                    const std::string condition(vartok->str() + op + vartok->strAt(2));
                    if (op == "==" && (num & num2) != ((bitop=='&') ? num2 : num))
                        assignIfError(assignTok, tok2, condition, false);
                    else if (op == "!=" && (num & num2) != ((bitop=='&') ? num2 : num))
                        assignIfError(assignTok, tok2, condition, true);
                }
            }

            bool ret1 = assignIfParseScope(assignTok, end->tokAt(2), varid, islocal, bitop, num);
            bool ret2 = false;
            if (Token::simpleMatch(end->next()->link(), "} else {"))
                ret2 = assignIfParseScope(assignTok, end->next()->link()->tokAt(3), varid, islocal, bitop, num);
            if (ret1 || ret2)
                return true;
        }
    }
    return false;
}

void CheckAssignIf::assignIfError(const Token *tok1, const Token *tok2, const std::string &condition, bool result)
{
    std::list<const Token *> locations;
    locations.push_back(tok1);
    locations.push_back(tok2);
#ifdef TSC_REPORT_NEW_ERRORTYPE
    reportError(locations,
                Severity::error,
                "logic",
                "assignIf","Mismatching assignment and comparison, comparison '" + condition + "' is always " + std::string(result ? "true" : "false") + ".");
#else
	reportError(locations,
		Severity::style,
		"assignIf",
		"Mismatching assignment and comparison, comparison '" + condition + "' is always " + std::string(result ? "true" : "false") + ".");
#endif
}

void CheckAssignIf::comparison()
{
    for (const Token *tok = _tokenizer->tokens(); tok; tok = tok->next()) {
        if (Token::Match(tok, "&|%or% %num% )| ==|!= %num% &&|%oror%|)")) {
            const MathLib::bigint num1 = MathLib::toLongNumber(tok->strAt(1));
            if (num1 < 0)
                continue;

            const Token *compareToken = tok->tokAt(2);
            if (compareToken->str() == ")") {
                if (!Token::Match(compareToken->link()->previous(), "(|%oror%|&&"))
                    continue;
                compareToken = compareToken->next();
            }

            const MathLib::bigint num2 = MathLib::toLongNumber(compareToken->strAt(1));
            if (num2 < 0)
                continue;

            if ((tok->str() == "&" && (num1 & num2) != num2) ||
                (tok->str() == "|" && (num1 | num2) != num2)) {
                const std::string& op(compareToken->str());
                comparisonError(tok, tok->str(), num1, op, num2, op=="==" ? false : true);
            }
        }
    }
}

void CheckAssignIf::comparisonError(const Token *tok, const std::string &bitop, MathLib::bigint value1, const std::string &op, MathLib::bigint value2, bool result)
{
    std::ostringstream expression;
    expression << std::hex << "(X " << bitop << " 0x" << value1 << ") " << op << " 0x" << value2;

    const std::string errmsg("Expression '" + expression.str() + "' is always " + (result?"true":"false") + ".\n"
                             "The expression '" + expression.str() + "' is always " + (result?"true":"false") +
                             ". Check carefully constants and operators used, these errors might be hard to "
                             "spot sometimes. In case of complex expression it might help to split it to "
                             "separate expressions.");
#ifdef TSC_REPORT_NEW_ERRORTYPE
    reportError(tok, Severity::error, "logic", "assignIf", errmsg);
#else
	reportError(tok, Severity::style, "comparisonError", errmsg);
#endif
}

void CheckAssignIf::multiCondition()
{
    const SymbolDatabase* const symbolDatabase = _tokenizer->getSymbolDatabase();

    for (std::list<Scope>::const_iterator i = symbolDatabase->scopeList.begin(); i != symbolDatabase->scopeList.end(); ++i) {
        if (i->type == Scope::eIf && Token::Match(i->classDef, "if ( %var% & %num% ) {")) {
            const Token* const tok = i->classDef;
			if(!tok)
				continue;
            const unsigned int varid(tok->tokAt(2)->varId());
            if (varid == 0)
                continue;

            const MathLib::bigint num1 = MathLib::toLongNumber(tok->strAt(4));
            if (num1 < 0)
                continue;

            const Token *tok2 = tok->linkAt(6);
            while (tok2 && Token::simpleMatch(tok2, "} else { if (")) {
                // Goto '('
                const Token * const opar = tok2->tokAt(4);

                // tok2: skip if-block
                tok2 = opar->link();
                if (Token::simpleMatch(tok2, ") {"))
                    tok2 = tok2->next()->link();

                // check condition..
                if (Token::Match(opar, "( %varid% ==|& %num% &&|%oror%|)", varid)) {
                    const MathLib::bigint num2 = MathLib::toLongNumber(opar->strAt(3));
                    if (num2 < 0)
                        continue;

                    if ((num1 & num2) == num2) {
                        multiConditionError(opar, tok->linenr());
                    }
                }
            }
        }
    }
}

void CheckAssignIf::multiConditionError(const Token *tok, unsigned int line1)
{
    std::ostringstream errmsg;
    errmsg << "Expression is always false because 'else if' condition matches previous condition at line "
           << line1 << ".";
#ifdef TSC_REPORT_NEW_ERRORTYPE
    reportError(tok, Severity::error, "logic",  "assignIf",errmsg.str());
#else
    reportError(tok, Severity::style, "multiCondition", errmsg.str());
#endif
}