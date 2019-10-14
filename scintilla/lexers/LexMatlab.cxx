// This file is part of Notepad2.
// See License.txt for details about distribution and modification.
//! Lexer for Matlab, Octave, Scilab and Gnuplot (treated as same as Octave).

#include <cstring>
#include <cassert>
#include <cctype>

#include "ILexer.h"
#include "Scintilla.h"
#include "SciLexer.h"

#include "WordList.h"
#include "LexAccessor.h"
#include "Accessor.h"
#include "StyleContext.h"
#include "CharacterSet.h"
#include "LexerModule.h"

using namespace Scintilla;

#define	LEX_MATLAB		40
#define	LEX_OCTAVE		61
#define	LEX_SCILAB		62
#define LEX_GNUPLOT		65
#define LEX_JULIA		66

static constexpr bool IsMatlabOctave(int lexType) noexcept {
	return lexType == LEX_MATLAB || lexType == LEX_OCTAVE;
}

static bool IsLineCommentStart(int lexType, const StyleContext &sc, int visibleChars) noexcept {
	const int ch = sc.ch;
	const int chNext = sc.chNext;
	return ch == '#'	// Octave, Julia, Gnuplot, Shebang or invalid character
		|| (IsMatlabOctave(lexType) && (ch == '%' || (visibleChars == 0 && ch == '.' && chNext == '.' && sc.GetRelative(2) == '.')))
		|| (lexType != LEX_JULIA && ch == '/' && chNext == '/'); // Scilab
}

static bool IsNestedCommentStart(int lexType, int ch, int chNext, int visibleChars, LexAccessor &styler, Sci_PositionU currentPos) noexcept {
	return visibleChars == 0 && chNext == '{'
		&& ((lexType == LEX_MATLAB && ch == '%') || (lexType == LEX_OCTAVE && (ch == '%' || ch == '#')))
		&& IsLexSpaceToEOL(styler, currentPos + 2);
}

static bool IsNestedCommentEnd(int lexType, int ch, int chNext, int visibleChars, LexAccessor &styler, Sci_PositionU currentPos) noexcept {
	return visibleChars == 0 && chNext == '}'
		&& ((lexType == LEX_MATLAB && ch == '%') || (lexType == LEX_OCTAVE && (ch == '%' || ch == '#')))
		&& IsLexSpaceToEOL(styler, currentPos + 2);
}

static bool IsBlockCommentStart(int lexType, int ch, int chNext, int visibleChars, LexAccessor &styler, Sci_PositionU currentPos) noexcept {
	return IsNestedCommentStart(lexType, ch, chNext, visibleChars, styler, currentPos)
		|| (lexType == LEX_JULIA && (ch == '#' && chNext == '='))
		|| (ch == '/' && chNext == '*'); // Scilab
}

static bool IsBlockCommentEnd(int lexType, int ch, int chNext, int visibleChars, LexAccessor &styler, Sci_PositionU currentPos) noexcept {
	return IsNestedCommentEnd(lexType, ch, chNext, visibleChars, styler, currentPos)
		|| (lexType == LEX_JULIA && (ch == '=' && chNext == '#'))
		|| (ch == '*' && chNext == '/'); // Scilab
}

static bool IsBlockCommentStart(int lexType, StyleContext &sc, int visibleChars) noexcept {
	return IsBlockCommentStart(lexType, sc.ch, sc.chNext, visibleChars, sc.styler, sc.currentPos);
}

static bool IsBlockCommentEnd(int lexType, StyleContext &sc, int visibleChars) noexcept {
	return IsBlockCommentEnd(lexType, sc.ch, sc.chNext, visibleChars, sc.styler, sc.currentPos);
}

static bool IsNestedCommentStart(int lexType, StyleContext &sc, int visibleChars) noexcept {
	return IsNestedCommentStart(lexType, sc.ch, sc.chNext, visibleChars, sc.styler, sc.currentPos);
}

static constexpr bool IsMatOperator(int ch) noexcept {
	return isoperator(ch) || ch == '@' || ch == '\\' || ch == '$';
}

// format: [.] digit [.] [e | E] [+ | -] [i | j]
static constexpr bool IsMatNumber(int ch, int chPrev) noexcept {
	return IsADigit(ch) || (ch == '.' && chPrev != '.') // only one dot
		|| ((ch == '+' || ch == '-') && (chPrev == 'e' || chPrev == 'E')) // exponent
		|| (IsADigit(chPrev) && (ch == 'e' || ch == 'E'
			|| ch == 'i' || ch == 'j' || ch == 'I' || ch == 'J'));// complex, 'I','J' in Octave
}

static constexpr bool IsInvalidFileName(int ch) noexcept {
	return isspacechar(ch) || ch == '<' || ch == '>' || ch == '/' || ch == '\\' || ch == '\'' || ch == '\"'
		|| ch == '|' || ch == '*' || ch == '?';
}

/*static const char * const matWordListDesc[] = {
	"Keywords",
	"Attributes",	// Matlab OOP, used to fold classdef file
	"Internal commands",
	"Function1",
	"Function2",
	"Function3",
	"Function4",
	0
};*/

static void ColouriseMatlabDoc(Sci_PositionU startPos, Sci_Position length, int initStyle, LexerWordList keywordLists, Accessor &styler) {
	const WordList &keywords = *keywordLists[0];
	const WordList &attributes = *keywordLists[1];
	const WordList &commands = *keywordLists[2];
	const WordList &function1 = *keywordLists[3];
	const WordList &function2 = *keywordLists[4];

	const int lexType = styler.GetPropertyInt("lexer.lang.type", LEX_MATLAB);

	Sci_Position lineCurrent = styler.GetLine(startPos);
	int commentLevel = (lineCurrent > 0) ? styler.GetLineState(lineCurrent - 1) : 0;
#define _UpdateLineState()	styler.SetLineState(lineCurrent, commentLevel)

	int visibleChars = 0;
	StyleContext sc(startPos, length, initStyle, styler);
	bool isTransposeOperator = false;

	bool hasTest = false; // Octave test/demo: %!demo %!test %testif %!assert %!error %!fail %!share %!function

	for (; sc.More(); sc.Forward()) {
		switch (sc.state) {
		case SCE_MAT_OPERATOR:
			sc.SetState(SCE_MAT_DEFAULT);
			if (sc.chPrev == '.') {
				if (sc.ch == '*' || sc.ch == '/' || sc.ch == '\\' || sc.ch == '^') {
					isTransposeOperator = false;
				} else if (sc.ch == '\'') {
					isTransposeOperator = true;
				}
			}
			break;
		case SCE_MAT_NUMBER:
			if (!IsMatNumber(sc.ch, sc.chPrev)) {
				if ((lexType == LEX_JULIA) && sc.ch == 'm' && sc.chPrev == 'i') {
					sc.Forward();
				}
				sc.SetState(SCE_MAT_DEFAULT);
				isTransposeOperator = true;
			}
			break;
		case SCE_MAT_HEXNUM:
			if (!IsHexDigit(sc.ch)) {
				sc.SetState(SCE_MAT_DEFAULT);
				isTransposeOperator = true;
			}
			break;
		case SCE_MAT_IDENTIFIER:
		case SCE_MAT_ATTRIBUTE:
			if (!iswordstart(sc.ch)) {
				char s[128];	// Matlab max indentifer length = 63, Octave unlimited
				sc.GetCurrent(s, sizeof(s));
				isTransposeOperator = true;		// only keywords are reserved

				if (keywords.InList(s)) {
					sc.ChangeState(SCE_MAT_KEYWORD);
					isTransposeOperator = false;
				} else if (attributes.InList(s)) {
					sc.ChangeState(SCE_MAT_ATTRIBUTE);
				} else if (commands.InList(s)) {
					sc.ChangeState(SCE_MAT_INTERNALCOMMAND);
				} else if (function1.InListPrefixed(s, '(')) {
					sc.ChangeState(SCE_MAT_FUNCTION1);
				} else if (function2.InListPrefixed(s, '(')) {
					sc.ChangeState(SCE_MAT_FUNCTION2);
				} else {
					const int chNext = sc.GetNextNSChar();
					if (chNext == '(') {
						sc.ChangeState(SCE_MAT_FUNCTION);
					} else if (lexType == LEX_JULIA && sc.state == SCE_MAT_IDENTIFIER && (chNext == '{')) {
						sc.ChangeState(SCE_MAT_ATTRIBUTE);
					}
				}
				if (sc.ch == '@') {
					sc.SetState(SCE_MAT_OPERATOR);
					sc.Forward();
				}
				sc.SetState(SCE_MAT_DEFAULT);
			}
			break;
		case SCE_MAT_CALLBACK:
		case SCE_MAT_VARIABLE:
			if (!iswordstart(sc.ch)) {
				if (sc.ch == '@') {
					sc.SetState(SCE_MAT_OPERATOR);
					sc.Forward();
				}
				sc.SetState(SCE_MAT_DEFAULT);
			}
			break;
		case SCE_MAT_COMMAND:
			if (IsInvalidFileName(sc.ch)) {
				sc.SetState(SCE_MAT_DEFAULT);
				isTransposeOperator = false;
			}
			break;
		case SCE_MAT_STRING:
			if ((lexType == LEX_JULIA) && sc.ch == '\\') {
				if (sc.chNext == '\"' || sc.chNext == '\'' || sc.chNext == '\\') {
					sc.Forward();
				}
			} else if (sc.ch == '\'') {
				if (sc.chNext == '\'') {
					sc.Forward();
				} else {
					sc.ForwardSetState(SCE_MAT_DEFAULT);
				}
			}
			break;
		case SCE_MAT_DOUBLEQUOTESTRING:
		case SCE_MAT_REGEX:
		case SCE_MAT_RAW_STRING2:
			if (sc.ch == '\\') {
				if (sc.chNext == '\"' || sc.chNext == '\'' || sc.chNext == '\\') {
					sc.Forward();
				}
			} else if (sc.ch == '\"') {
				if (sc.state == SCE_MAT_REGEX) {
					while (sc.chNext == 'i' || sc.chNext == 'm' || sc.chNext == 's' || sc.chNext == 'x') {
						sc.Forward();
					}
				}
				sc.ForwardSetState(SCE_MAT_DEFAULT);
			}
			break;
		case SCE_MAT_TRIPLE_STRING2:
			if (sc.Match(R"(""")")) {
				sc.Forward(2);
				sc.ForwardSetState(SCE_MAT_DEFAULT);
			}
			break;
		case SCE_MAT_BACKTICK:
			if (sc.ch == '`') {
				sc.ForwardSetState(SCE_MAT_DEFAULT);
			}
			break;
		case SCE_MAT_COMMENTBLOCK:
			if (IsBlockCommentEnd(lexType, sc, visibleChars)) {
				if (IsMatlabOctave(lexType)) {
					--commentLevel;
					if (commentLevel < 0) {
						commentLevel = 0;
					}
				}
				if (commentLevel == 0) {
					sc.Forward();
					sc.ForwardSetState(SCE_MAT_DEFAULT);
				}
			} else if (IsNestedCommentStart(lexType, sc, visibleChars)) {
				++commentLevel;
				sc.Forward();
			}
			break;
		case SCE_MAT_COMMENT:
			if (sc.atLineStart) {
				visibleChars = 0;
				sc.SetState(SCE_MAT_DEFAULT);
				isTransposeOperator = false;
			}
			break;
		}

		if (sc.state == SCE_MAT_DEFAULT) {
			if ((lexType == LEX_JULIA) && sc.Match('r', '\"')) { // regex
				sc.SetState(SCE_MAT_REGEX);
				sc.Forward();
			} else if ((lexType == LEX_JULIA) && (sc.ch == 'b' || sc.ch == 'L' || sc.ch == 'I' || sc.ch == 'E' || sc.ch == 'v') && sc.chNext == '\"') {
				sc.SetState(SCE_MAT_DOUBLEQUOTESTRING);
				sc.Forward();
			} else if (sc.Match("raw\"")) {
				sc.SetState(SCE_MAT_RAW_STRING2);
				sc.Forward(3);
				if (sc.Match(R"(""")")) {
					sc.ChangeState(SCE_MAT_TRIPLE_STRING2);
					sc.Forward(2);
				}
			} else if (IsBlockCommentStart(lexType, sc, visibleChars)) {
				if (IsMatlabOctave(lexType)) {
					++commentLevel;
				}
				sc.SetState(SCE_MAT_COMMENTBLOCK);
				sc.Forward();
			} else if (IsLineCommentStart(lexType, sc, visibleChars)) {
				{
					sc.SetState(SCE_MAT_COMMENT);
					// Octave demo/test section, always placed in end of file
					if ((lexType == LEX_OCTAVE) && sc.atLineStart && sc.ch == '%' && sc.chNext == '!') {
						const Sci_Position pos = static_cast<Sci_Position>(sc.currentPos) + 2;
						if (!hasTest && (styler.Match(pos, "test") || styler.Match(pos, "demo")
							|| styler.Match(pos, "assert") || styler.Match(pos, "error") || styler.Match(pos, "warning")
							|| styler.Match(pos, "fail") || styler.Match(pos, "shared") || styler.Match(pos, "function"))) {
							hasTest = true;
						}
						if (hasTest) {
							sc.Forward(2);
							if (iswordstart(sc.ch)) {
								sc.SetState(SCE_MAT_IDENTIFIER);
							} else {
								sc.SetState(SCE_MAT_DEFAULT);
							}
						}
					} else if (sc.ch == '.') {
						sc.Forward(2);
					}
				}
			} else if (IsMatlabOctave(lexType) && visibleChars == 0 && sc.ch == '!') {
				sc.SetState(SCE_MAT_COMMAND);
			} else if (sc.Match(R"(""")")) {
				sc.SetState(SCE_MAT_TRIPLE_STRING2);
				sc.Forward(2);
			} else if (sc.ch == '\'') { // Octave allows whitespace before transpose operator
				if (isTransposeOperator) {
					sc.SetState(SCE_MAT_OPERATOR);
				} else {
					sc.SetState(SCE_MAT_STRING);
				}
			} else if (sc.ch == '\"') {
				sc.SetState(SCE_MAT_DOUBLEQUOTESTRING);
			} else if (sc.ch == '`') {
				sc.SetState(SCE_MAT_BACKTICK);
			} else if (sc.ch == '0' && (sc.chNext == 'x' || sc.chNext == 'X')) {
				sc.SetState(SCE_MAT_HEXNUM);
				sc.Forward();
			} else if (IsADigit(sc.ch) || (sc.ch == '.' && IsADigit(sc.chNext))) {
				sc.SetState(SCE_MAT_NUMBER);
			} else if (sc.ch == '@' && iswordstart(sc.chNext)) {
				sc.SetState(SCE_MAT_CALLBACK);
				sc.Forward();
			} else if (sc.ch == '$' && iswordstart(sc.chNext)) {
				sc.SetState(SCE_MAT_VARIABLE);
				sc.Forward();
			} else if (iswordstart(sc.ch)) {
				sc.SetState(SCE_MAT_IDENTIFIER);
			} else if (IsMatOperator(sc.ch)) {
				sc.SetState(SCE_MAT_OPERATOR);
				if (sc.ch == ')' || sc.ch == ']' || sc.ch == '}') {
					isTransposeOperator = true;
				} else {
					isTransposeOperator = false;
				}

				if (lexType == LEX_JULIA && (sc.ch == ':' || sc.ch == '<') && sc.chNext == ':') {
					// var::Type, T <: Type
					sc.Forward(2);
					sc.SetState(SCE_MAT_DEFAULT);
					while (IsSpaceOrTab(sc.ch)) {
						sc.Forward();
					}
					sc.SetState(SCE_MAT_ATTRIBUTE);
				}
			} else {
				isTransposeOperator = false;
			}
		}

		if (sc.atLineEnd) {
			_UpdateLineState();
			++lineCurrent;
			visibleChars = 0;
		}
		if (!isspacechar(sc.ch)) {
			visibleChars++;
		}

	}

	sc.Complete();
}

// character after the "end" statement (skiped space and tabs)
static constexpr bool IsMatEndChar(char chEnd, int style) noexcept {
	return (chEnd == '\r' || chEnd == '\n' || chEnd == ';')
		|| (style == SCE_MAT_COMMENT || style == SCE_MAT_COMMENTBLOCK);
}

static constexpr bool IsStreamCommentStyle(int style) noexcept {
	return style == SCE_MAT_COMMENTBLOCK;
}

static constexpr bool IsSripleStringStyle(int style) noexcept {
	return style == SCE_MAT_TRIPLE_STRING2;
}

#define IsCommentLine(line)		IsLexCommentLine(line, styler, SCE_MAT_COMMENT)
#define StrEqu(str1, str2)		(strcmp(str1, str2) == 0)

static void FoldMatlabDoc(Sci_PositionU startPos, Sci_Position length, int initStyle, LexerWordList, Accessor &styler) {
	const int lexType = styler.GetPropertyInt("lexer.lang.type", LEX_MATLAB);
	const bool foldComment = styler.GetPropertyInt("fold.comment") != 0;
	const bool foldCompact = styler.GetPropertyInt("fold.compact", 1) != 0;

	const Sci_PositionU endPos = startPos + length;
	int visibleChars = 0;
	int numBrace = 0;
	Sci_Position lineCurrent = styler.GetLine(startPos);
	int levelCurrent = SC_FOLDLEVELBASE;
	if (lineCurrent > 0)
		levelCurrent = styler.LevelAt(lineCurrent - 1) >> 16;
	int levelNext = levelCurrent;

	char ch = '\0';
	char chNext = styler[startPos];
	int style = initStyle;
	int styleNext = styler.StyleAt(startPos);

	for (Sci_PositionU i = startPos; i < endPos; i++) {
		const char chPrev = ch;
		ch = chNext;
		chNext = styler.SafeGetCharAt(i + 1);
		const int stylePrev = style;
		style = styleNext;
		styleNext = styler.StyleAt(i + 1);
		const bool atEOL = (ch == '\r' && chNext != '\n') || (ch == '\n');

		if (foldComment && IsStreamCommentStyle(style)) {
			if (IsMatlabOctave(lexType)) {
				if (IsNestedCommentStart(lexType, ch, chNext, visibleChars, styler, i)) {
					levelNext++;
				} else if (IsNestedCommentEnd(lexType, ch, chNext, visibleChars, styler, i)) {
					levelNext--;
				}
			} else {
				if (!IsStreamCommentStyle(stylePrev)) {
					levelNext++;
				} else if (!IsStreamCommentStyle(styleNext) && !atEOL) {
					levelNext--;
				}
			}
		}
		if (foldComment && atEOL && IsCommentLine(lineCurrent)) {
			if (!IsCommentLine(lineCurrent - 1) && IsCommentLine(lineCurrent + 1))
				levelNext++;
			else if (IsCommentLine(lineCurrent - 1) && !IsCommentLine(lineCurrent + 1))
				levelNext--;
		}
		if (foldComment && IsSripleStringStyle(style)) {
			if (!IsSripleStringStyle(stylePrev)) {
				levelNext++;
			} else if (!IsSripleStringStyle(styleNext) && !atEOL) {
				levelNext--;
			}
		}

		if (style == SCE_MAT_KEYWORD && stylePrev != SCE_MAT_KEYWORD && numBrace == 0 && chPrev != '.' && chPrev != ':') {
			char word[32];
			const Sci_PositionU len = LexGetRange(i, styler, iswordstart, word, sizeof(word));
			if ((StrEqu(word, "function") && ((lexType == LEX_JULIA) || (!(lexType == LEX_JULIA) && LexGetNextChar(i + len, styler) != '(')))
				|| StrEqu(word, "if")
				|| StrEqu(word, "for")
				|| StrEqu(word, "while")
				|| StrEqu(word, "try")
				|| (IsMatlabOctave(lexType) && (StrEqu(word, "switch") || StrEqu(word, "classdef") || StrEqu(word, "parfor")))
				|| ((lexType == LEX_OCTAVE) && (StrEqu(word, "do") || StrEqu(word, "unwind_protect")))
				|| ((lexType == LEX_SCILAB) && StrEqu(word, "select"))
				|| ((lexType == LEX_JULIA) && (StrEqu(word, "type") || StrEqu(word, "quote")
					|| StrEqu(word, "let") || StrEqu(word, "macro") || StrEqu(word, "do")
					|| StrEqu(word, "struct")
					|| StrEqu(word, "begin") || StrEqu(word, "module"))
					)
				) {
				levelNext++;
			} else if ((lexType == LEX_OCTAVE) && StrEqu(word, "until")) {
				levelNext--;
			} else if (styler.Match(i, "end")) {
				levelNext--;
				//if (len == 3) {	// just "end"
				//	Sci_Position pos = LexSkipSpaceTab(i+3, endPos, styler);
				//	char chEnd = styler.SafeGetCharAt(pos);
				//	if (!(IsMatEndChar(chEnd, styler.StyleAt(pos)))) {
				//		levelNext++;
				//	}
				//}
			} else if (IsMatlabOctave(lexType) && chPrev != '@' && (StrEqu(word, "methods")
				|| StrEqu(word, "properties") || StrEqu(word, "events") || StrEqu(word, "enumeration"))) {
				// Matlab classdef
				Sci_Position pos = LexSkipSpaceTab(i + len, endPos, styler);
				const char chEnd = styler.SafeGetCharAt(pos);
				if (IsMatEndChar(chEnd, styler.StyleAt(pos))) {
					levelNext++;
				} else if (chEnd == '(') {
					pos++;
					pos = LexSkipSpaceTab(pos, endPos, styler);
					if (styler.StyleAt(pos) == SCE_MAT_ATTRIBUTE)
						levelNext++;
				}
			}
		}

		if (style == SCE_MAT_OPERATOR) { // too many () [] {}
			if (ch == '{' || ch == '[' || ch == '(') {
				levelNext++;
				numBrace++;
			} else if (ch == '}' || ch == ']' || ch == ')') {
				levelNext--;
				numBrace--;
			}
		}

		if (!isspacechar(ch))
			visibleChars++;

		if (atEOL || (i == endPos - 1)) {
			const int levelUse = levelCurrent;
			int lev = levelUse | levelNext << 16;
			if (visibleChars == 0 && foldCompact)
				lev |= SC_FOLDLEVELWHITEFLAG;
			if (levelUse < levelNext)
				lev |= SC_FOLDLEVELHEADERFLAG;
			if (lev != styler.LevelAt(lineCurrent)) {
				styler.SetLevel(lineCurrent, lev);
			}
			lineCurrent++;
			levelCurrent = levelNext;
			visibleChars = 0;
		}
	}
}

LexerModule lmMatlab(SCLEX_MATLAB, ColouriseMatlabDoc, "matlab", FoldMatlabDoc);
