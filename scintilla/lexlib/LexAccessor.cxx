// Scintilla source code edit control
/** @file LexAccessor.cxx
 ** Interfaces between Scintilla and lexers.
 **/
// Copyright 1998-2010 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#include <cassert>
#include <cctype>

#include "ILexer.h"
#include "LexAccessor.h"
#include "CharacterSet.h"

using namespace Scintilla;

namespace Scintilla {

bool LexAccessor::MatchIgnoreCase(Sci_Position pos, const char *s) noexcept {
	for (int i = 0; *s; i++) {
		if (*s != MakeLowerCase(SafeGetCharAt(pos + i))) {
			return false;
		}
		s++;
	}
	return true;
}

Sci_Position LexLineSkipSpaceTab(Sci_Position line, LexAccessor &styler) noexcept {
	const Sci_Position startPos = styler.LineStart(line);
	const Sci_Position endPos = styler.LineStart(line + 1) - 1;
	for (Sci_Position i = startPos; i < endPos; i++) {
		if (!IsSpaceOrTab(styler.SafeGetCharAt(i))) {
			return i;
		}
	}
	return endPos;
}

bool IsLexSpaceToEOL(LexAccessor &styler, Sci_Position startPos) noexcept {
	const Sci_Position line = styler.GetLine(startPos);
	const Sci_Position endPos = styler.LineStart(line + 1) - 1;
	for (Sci_Position i = startPos; i < endPos; i++) {
		if (!IsSpaceOrTab(styler.SafeGetCharAt(i))) {
			return false;
		}
	}
	return true;
}

bool IsLexEmptyLine(LexAccessor &styler, Sci_Position line) noexcept {
	const Sci_Position startPos = styler.LineStart(line);
	const Sci_Position endPos = styler.LineStart(line + 1) - 1;
	for (Sci_Position i = startPos; i < endPos; i++) {
		if (!IsSpaceOrTab(styler.SafeGetCharAt(i))) {
			return false;
		}
	}
	return true;
}

bool IsLexLineStartsWith(Sci_Position line, LexAccessor &styler, const char *word, bool matchCase, int style) noexcept {
	const Sci_Position startPos = styler.LineStart(line);
	const Sci_Position endPos = styler.LineStart(line + 1) - 1;
	for (Sci_Position pos = startPos; pos < endPos; pos++) {
		if (!IsSpaceOrTab(styler.SafeGetCharAt(pos))) {
			return styler.StyleAt(pos) == style &&
			(matchCase ? styler.Match(pos, word) : styler.MatchIgnoreCase(pos, word));
		}
	}
	return false;
}

bool IsLexCommentLine(Sci_Position line, LexAccessor &styler, int style) noexcept {
	const Sci_Position startPos = styler.LineStart(line);
	const Sci_Position endPos = styler.LineStart(line + 1) - 1;
	for (Sci_Position pos = startPos; pos < endPos; pos++) {
		if (!IsSpaceOrTab(styler.SafeGetCharAt(pos))) {
			const int stl = styler.StyleAt(pos);
			//char ch = styler.SafeGetCharAt(pos);
			if (stl == 0 && stl != style) {
				return false;
			}
			while (style != 0 && stl != (style & 0xFF)) {
				style >>= 8;
			}
			return style != 0;
		}
	}
	return false;
}

Sci_Position LexSkipWhiteSpace(Sci_Position startPos, Sci_Position endPos, LexAccessor &styler) noexcept {
	for (Sci_Position i = startPos; i < endPos; i++) {
		if (!(IsWhiteSpace(styler.SafeGetCharAt(i)))) {
			return i;
		}
	}
	return endPos;
}

Sci_Position LexSkipWhiteSpace(Sci_Position startPos, Sci_Position endPos, LexAccessor &styler, bool IsStreamCommentStyle(int)) noexcept {
	for (Sci_Position i = startPos; i < endPos; i++) {
		if (!(IsWhiteSpace(styler.SafeGetCharAt(i)) || IsStreamCommentStyle(styler.StyleAt(i)))) {
			return i;
		}
	}
	return endPos;
}

Sci_Position LexSkipWhiteSpace(Sci_Position startPos, Sci_Position endPos, LexAccessor &styler,
	bool IsStreamCommentStyle(int), const CharacterSet &charSet) noexcept {
	for (Sci_Position i = startPos; i < endPos; i++) {
		const char ch = styler.SafeGetCharAt(i);
		if (!(IsWhiteSpace(ch) || charSet.Contains(ch) || IsStreamCommentStyle(styler.StyleAt(i)))) {
			return i;
		}
	}
	return endPos;
}

Sci_PositionU LexGetRange(Sci_Position startPos, Sci_Position endPos, LexAccessor &styler, char *s, Sci_PositionU len) noexcept {
	Sci_PositionU i = 0;
	char ch = styler.SafeGetCharAt(startPos + i);
	Sci_PositionU _endPos = endPos - startPos + 1;
	if (_endPos > len - 1) {
		_endPos = len - 1;
	}
	while (i < _endPos) {
		s[i] = ch;
		i++;
		ch = styler.SafeGetCharAt(startPos + i);
	}
	s[i] = '\0';
	return i;
}

Sci_PositionU LexGetRangeLowered(Sci_Position startPos, Sci_Position endPos, LexAccessor &styler, char *s, Sci_PositionU len) noexcept {
	Sci_PositionU i = 0;
	char ch = MakeLowerCase(styler.SafeGetCharAt(startPos + i));
	Sci_PositionU _endPos = endPos - startPos + 1;
	if (_endPos > len - 1) {
		_endPos = len - 1;
	}
	while (i < _endPos) {
		s[i] = ch;
		i++;
		ch = MakeLowerCase(styler.SafeGetCharAt(startPos + i));
	}
	s[i] = '\0';
	return i;
}

Sci_PositionU LexGetRange(Sci_Position startPos, LexAccessor &styler, bool IsWordChar(int), char *s, Sci_PositionU len) noexcept {
	Sci_PositionU i = 0;
	char ch = styler.SafeGetCharAt(startPos + i);
	while ((i < len - 1) && IsWordChar(static_cast<unsigned char>(ch))) {
		s[i] = ch;
		i++;
		ch = styler.SafeGetCharAt(startPos + i);
	}
	s[i] = '\0';
	return i;
}

Sci_PositionU LexGetRangeLowered(Sci_Position startPos, LexAccessor &styler, bool IsWordChar(int), char *s, Sci_PositionU len) noexcept {
	Sci_PositionU i = 0;
	char ch = MakeLowerCase(styler.SafeGetCharAt(startPos + i));
	while ((i < len - 1) && IsWordChar(static_cast<unsigned char>(ch))) {
		s[i] = ch;
		i++;
		ch = MakeLowerCase(styler.SafeGetCharAt(startPos + i));
	}
	s[i] = '\0';
	return i;
}

Sci_PositionU LexGetRange(Sci_Position startPos, LexAccessor &styler, const CharacterSet &charSet, char *s, Sci_PositionU len) noexcept {
	Sci_PositionU i = 0;
	char ch = styler.SafeGetCharAt(startPos + i);
	while ((i < len - 1) && charSet.Contains(ch)) {
		s[i] = ch;
		i++;
		ch = styler.SafeGetCharAt(startPos + i);
	}
	s[i] = '\0';
	return i;
}

Sci_PositionU LexGetRangeLowered(Sci_Position startPos, LexAccessor &styler, const CharacterSet &charSet, char *s, Sci_PositionU len) noexcept {
	Sci_PositionU i = 0;
	char ch = MakeLowerCase(styler.SafeGetCharAt(startPos + i));
	while ((i < len - 1) && charSet.Contains(ch)) {
		s[i] = ch;
		i++;
		ch = MakeLowerCase(styler.SafeGetCharAt(startPos + i));
	}
	s[i] = '\0';
	return i;
}

}
