/* @@@LICENSE
*
*      Copyright (c) 2026 webOS Community Edition
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* LICENSE@@@ */

// [CE] An emoji font in the system UI's font fallback (webos-doctor-ce Docs/EMOJI-FONT.md §5).
//
// Qt draws the system UI (banners, card titles, the status bar) through HP's qpalm platform
// plugin, whose QBasicUnixFontDatabase::fallbacksForFamily() returns a hard-coded list: a style
// font, then Hei S / HeiT / Heisei Kaku Gothic / Dotum. A font dropped into /usr/share/fonts is
// registered but never asked for a missing glyph, so emoji draw as boxes (seen on a TouchPad).
// The plugin's vtable reaches that function by relative address, so defining the symbol here
// would not override it. Instead, at startup, the vtable slot holding the function is pointed
// at a wrapper that calls the original and appends the emoji font's family.
//
// Everything is found by symbol and checked: the vtable, the function, exactly one slot holding
// it. On any surprise nothing is changed. If the font isn't installed, Qt drops the unknown
// family from the list (qfontdatabase_qpa.cpp fallbackFamilies()), so the wrapper is harmless.

#include "QtEmojiFallback.h"

#include <dlfcn.h>
#include <glib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#include <QFont>
#include <QString>
#include <QStringList>

namespace {

const char cVtableSymbol[] = "_ZTV22QBasicUnixFontDatabase";
const char cFunctionSymbol[] =
	"_ZNK22QBasicUnixFontDatabase18fallbacksForFamilyE7QStringRKN5QFont5StyleERKNS1_9StyleHintERKN14QUnicodeTables6ScriptE";
const char cEmojiFamily[] = "Noto Emoji";	// the family name inside /usr/share/fonts/CE-Emoji.ttf
const int cMaxVtableSlots = 64;

// Same parameter list and constness as QBasicUnixFontDatabase::fallbacksForFamily, as a member
// function, so the compiler passes this/result/arguments exactly as Qt's caller does.
// (The last parameter is `const QUnicodeTables::Script &`: a reference to an enum, i.e. a pointer.)
class FallbackHook
{
public:
	QStringList fallbacksForFamily(const QString family, const QFont::Style & style,
								   const QFont::StyleHint & styleHint, const int & script) const;
};

typedef QStringList (FallbackHook::*FallbacksFn)(const QString, const QFont::Style &,
												  const QFont::StyleHint &, const int &) const;

// GCC's pointer-to-member-function: { function address, this adjustment (ARM: <<1 | virtual) }
union MemberFn
{
	FallbacksFn fn;
	struct { void * ptr; ptrdiff_t adj; } raw;
};

FallbacksFn s_original = 0;

QStringList FallbackHook::fallbacksForFamily(const QString family, const QFont::Style & style,
											 const QFont::StyleHint & styleHint, const int & script) const
{
	QStringList list = (this->*s_original)(family, style, styleHint, script);
	if (!list.contains(QString::fromLatin1(cEmojiFamily)))
		list << QString::fromLatin1(cEmojiFamily);
	return list;
}

// The page's current protection, from /proc/self/maps (PROT_* bits), or -1 if not found.
int pageProtection(uintptr_t addr)
{
	FILE * maps = fopen("/proc/self/maps", "r");
	if (!maps)
		return -1;
	char line[512];
	int prot = -1;
	while (prot < 0 && fgets(line, sizeof(line), maps))
	{
		unsigned long lo, hi;
		char perms[5];
		if (sscanf(line, "%lx-%lx %4s", &lo, &hi, perms) == 3 && addr >= lo && addr < hi)
			prot = (perms[0] == 'r' ? PROT_READ : 0) | (perms[1] == 'w' ? PROT_WRITE : 0) | (perms[2] == 'x' ? PROT_EXEC : 0);
	}
	fclose(maps);
	return prot;
}

// Write one word into a vtable, leaving the page's protection as it found it. HP's libqpalm
// has no RELRO: its vtables share a writable page with .got/.data/.bss, so the page must NOT
// be left read-only (that crashed QEglFSWindow's constructor on a TouchPad).
bool writeWord(void ** slot, void * value)
{
	uintptr_t addr = reinterpret_cast<uintptr_t>(slot);
	int prot = pageProtection(addr);
	if (prot < 0)
		return false;
	if (prot & PROT_WRITE)
	{
		*slot = value;
		return true;
	}
	long page = sysconf(_SC_PAGESIZE);
	void * start = reinterpret_cast<void *>(addr & ~(uintptr_t)(page - 1));
	if (mprotect(start, page, prot | PROT_WRITE) != 0)
		return false;
	*slot = value;
	mprotect(start, page, prot);
	return true;
}

} // namespace

void installQtEmojiFallback()
{
	void ** vtable = static_cast<void **>(dlsym(RTLD_DEFAULT, cVtableSymbol));
	void * original = dlsym(RTLD_DEFAULT, cFunctionSymbol);
	if (!vtable || !original)
	{
		g_warning("QtEmojiFallback: %s or its fallbacksForFamily not found; emoji fallback off", cVtableSymbol);
		return;
	}
	// the vtable symbol starts with offset-to-top and the typeinfo pointer
	void ** slot = 0;
	for (int i = 2; i < 2 + cMaxVtableSlots; ++i)
	{
		if (vtable[i] == original)
		{
			if (slot)
			{
				g_warning("QtEmojiFallback: fallbacksForFamily found twice in the vtable; emoji fallback off");
				return;
			}
			slot = &vtable[i];
		}
	}
	if (!slot)
	{
		g_warning("QtEmojiFallback: fallbacksForFamily not in the vtable; emoji fallback off");
		return;
	}
	MemberFn orig;
	orig.raw.ptr = original;
	orig.raw.adj = 0;
	s_original = orig.fn;
	MemberFn hook;
	hook.fn = &FallbackHook::fallbacksForFamily;
	if (!writeWord(slot, hook.raw.ptr))
	{
		g_warning("QtEmojiFallback: can't write the vtable slot; emoji fallback off");
		return;
	}
	g_message("QtEmojiFallback: '%s' appended to the system UI's fallback fonts", cEmojiFamily);
}
