#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace text {

// Encodes UTF-8 Russian text into the exact SanLtd / SL font byte table
// verified against modloader\Russificator\text\american.gxt.
// GTA SA color/formatting tokens like ~r~, ~g~, ~w~, ~y~, ~n~ pass through untouched.
inline std::string ToGameRussian(std::string_view utf8) {
    std::string out;
    out.reserve(utf8.size());

    size_t i = 0;
    while (i < utf8.size()) {
        uint8_t c0 = static_cast<uint8_t>(utf8[i]);
        if (c0 < 0x80) {
            out.push_back(static_cast<char>(c0));
            ++i;
            continue;
        }

        // Decode 2-byte UTF-8 Cyrillic (U+0400..U+04FF)
        if ((c0 & 0xE0) == 0xC0 && i + 1 < utf8.size()) {
            uint8_t c1 = static_cast<uint8_t>(utf8[i + 1]);
            uint32_t cp = ((c0 & 0x1Fu) << 6) | (c1 & 0x3Fu);
            i += 2;

            char mapped = '?';
            switch (cp) {
                // Uppercase А..Я + Ё
                case 0x0410: mapped = 'A';    break; // А
                case 0x0411: mapped = '\x80'; break; // Б
                case 0x0412: mapped = '\x8B'; break; // В
                case 0x0413: mapped = '\x82'; break; // Г
                case 0x0414: mapped = '\x83'; break; // Д
                case 0x0415: mapped = 'E';    break; // Е
                case 0x0401: mapped = 'E';    break; // Ё
                case 0x0416: mapped = '\x84'; break; // Ж
                case 0x0417: mapped = '\x88'; break; // З
                case 0x0418: mapped = '\x85'; break; // И
                case 0x0419: mapped = '\x86'; break; // Й
                case 0x041A: mapped = 'K';    break; // К
                case 0x041B: mapped = '\x87'; break; // Л
                case 0x041C: mapped = '\x96'; break; // М
                case 0x041D: mapped = '\xAD'; break; // Н
                case 0x041E: mapped = 'O';    break; // О
                case 0x041F: mapped = '\x8C'; break; // П
                case 0x0420: mapped = 'P';    break; // Р
                case 0x0421: mapped = 'C';    break; // С
                case 0x0422: mapped = '\x8F'; break; // Т
                case 0x0423: mapped = 'Y';    break; // У
                case 0x0424: mapped = '\x81'; break; // Ф
                case 0x0425: mapped = 'X';    break; // Х
                case 0x0426: mapped = '\x89'; break; // Ц
                case 0x0427: mapped = '\x8D'; break; // Ч
                case 0x0428: mapped = '\x8E'; break; // Ш
                case 0x0429: mapped = '\x8A'; break; // Щ
                case 0x042A: mapped = '\xA7'; break; // Ъ
                case 0x042B: mapped = '\x91'; break; // Ы
                case 0x042C: mapped = '\x92'; break; // Ь
                case 0x042D: mapped = '\x93'; break; // Э
                case 0x042E: mapped = '\x94'; break; // Ю
                case 0x042F: mapped = '\x95'; break; // Я

                // Lowercase а..я + ё
                case 0x0430: mapped = 'a';    break; // а
                case 0x0431: mapped = '\x97'; break; // б
                case 0x0432: mapped = '\xA2'; break; // в
                case 0x0433: mapped = '\x99'; break; // г
                case 0x0434: mapped = '\x9A'; break; // д
                case 0x0435: mapped = 'e';    break; // е
                case 0x0451: mapped = 'e';    break; // ё
                case 0x0436: mapped = '\x9B'; break; // ж
                case 0x0437: mapped = '\x9F'; break; // з
                case 0x0438: mapped = '\x9C'; break; // и
                case 0x0439: mapped = '\x9D'; break; // й
                case 0x043A: mapped = 'k';    break; // к
                case 0x043B: mapped = '\x9E'; break; // л
                case 0x043C: mapped = '\xAF'; break; // м
                case 0x043D: mapped = '\xAE'; break; // н
                case 0x043E: mapped = 'o';    break; // о
                case 0x043F: mapped = '\xA3'; break; // п
                case 0x0440: mapped = 'p';    break; // р
                case 0x0441: mapped = 'c';    break; // с
                case 0x0442: mapped = '\xA6'; break; // т
                case 0x0443: mapped = 'y';    break; // у
                case 0x0444: mapped = '\x98'; break; // ф
                case 0x0445: mapped = 'x';    break; // х
                case 0x0446: mapped = '\xA0'; break; // ц
                case 0x0447: mapped = '\xA4'; break; // ч
                case 0x0448: mapped = '\xA5'; break; // ш
                case 0x0449: mapped = '\xA1'; break; // щ
                case 0x044A: mapped = '\xA7'; break; // ъ
                case 0x044B: mapped = '\xA8'; break; // ы
                case 0x044C: mapped = '\xA9'; break; // ь
                case 0x044D: mapped = '\xAA'; break; // э
                case 0x044E: mapped = '\xAB'; break; // ю
                case 0x044F: mapped = '\xAC'; break; // я

                default: mapped = ' '; break;
            }
            out.push_back(mapped);
            continue;
        }

        // Skip other multi-byte UTF-8 sequences gracefully
        if ((c0 & 0xF0) == 0xE0) i += 3;
        else if ((c0 & 0xF8) == 0xF0) i += 4;
        else ++i;
        out.push_back(' ');
    }

    return out;
}

} // namespace text
