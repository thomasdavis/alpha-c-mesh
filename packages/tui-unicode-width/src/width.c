/* unicode-width 0.2.2's str_width and lookup_width, ported: the width of each character depends on
 * the one after it (WidthInfo), so the string is walked backwards, as the crate's rfold does. */
#include "tui/unicode_width.h"

#include "uw_tables.h"
#include "tui/utf8.h"

#include <stdbool.h>
#include <stdlib.h>

typedef uint16_t info;

enum {
    LIGATURE_TRANSPARENT_MASK = 0x2000,
};

#define I_DEFAULT 0x0000
#define I_LINE_FEED 0x0001
#define I_EMOJI_MODIFIER 0x0002
#define I_REGIONAL_INDICATOR 0x0003
#define I_SEVERAL_REGIONAL_INDICATOR 0x0004
#define I_EMOJI_PRESENTATION 0x0005
#define I_ZWJ_EMOJI_PRESENTATION 0x1006
#define I_VS16_ZWJ_EMOJI_PRESENTATION 0x9006
#define I_KEYCAP_ZWJ_EMOJI_PRESENTATION 0x1007
#define I_VS16_KEYCAP_ZWJ_EMOJI_PRESENTATION 0x9007
#define I_REGIONAL_INDICATOR_ZWJ_PRESENTATION 0x0009
#define I_EVEN_REGIONAL_INDICATOR_ZWJ_PRESENTATION 0x000A
#define I_ODD_REGIONAL_INDICATOR_ZWJ_PRESENTATION 0x000B
#define I_TAG_END_ZWJ_EMOJI_PRESENTATION 0x0010
#define I_TAG_D1_END_ZWJ_EMOJI_PRESENTATION 0x0011
#define I_TAG_D2_END_ZWJ_EMOJI_PRESENTATION 0x0012
#define I_TAG_D3_END_ZWJ_EMOJI_PRESENTATION 0x0013
#define I_TAG_A1_END_ZWJ_EMOJI_PRESENTATION 0x0019
#define I_TAG_A2_END_ZWJ_EMOJI_PRESENTATION 0x001A
#define I_TAG_A3_END_ZWJ_EMOJI_PRESENTATION 0x001B
#define I_TAG_A4_END_ZWJ_EMOJI_PRESENTATION 0x001C
#define I_TAG_A5_END_ZWJ_EMOJI_PRESENTATION 0x001D
#define I_TAG_A6_END_ZWJ_EMOJI_PRESENTATION 0x001E
#define I_KIRAT_RAI_VOWEL_SIGN_E 0x0020
#define I_KIRAT_RAI_VOWEL_SIGN_AI 0x0021
#define I_VARIATION_SELECTOR_1_2_OR_3 0x0200
#define I_VARIATION_SELECTOR_15 0x4000
#define I_VARIATION_SELECTOR_16 0x8000
#define I_JOINING_GROUP_ALEF 0x30FF
#define I_HEBREW_LETTER_LAMED 0x3800
#define I_ZWJ_HEBREW_LETTER_LAMED 0x3C00
#define I_BUGINESE_LETTER_YA 0x3801
#define I_ZWJ_BUGINESE_LETTER_YA 0x3C01
#define I_BUGINESE_VOWEL_SIGN_I_ZWJ_LETTER_YA 0x3C02
#define I_TIFINAGH_CONSONANT 0x3803
#define I_ZWJ_TIFINAGH_CONSONANT 0x3C03
#define I_TIFINAGH_JOINER_CONSONANT 0x3C04
#define I_LISU_TONE_LETTER_MYA_NA_JEU 0x3C05
#define I_OLD_TURKIC_LETTER_ORKHON_I 0x3806
#define I_ZWJ_OLD_TURKIC_LETTER_ORKHON_I 0x3C06
#define I_KHMER_COENG_ELIGIBLE_LETTER 0x3C07

static bool is_ligature_transparent_info(info i) { return (i & 0x0800) == 0x0800; }
static info set_zwj_bit(info i) { return (info)(i | 0x0400); }
static bool is_emoji_presentation(info i) { return (i & I_VARIATION_SELECTOR_16) == I_VARIATION_SELECTOR_16; }
static bool is_zwj_emoji_presentation(info i) { return (i & 0xB000) == 0x9000; }
static info set_emoji_presentation(info i) {
    if ((i & LIGATURE_TRANSPARENT_MASK) == LIGATURE_TRANSPARENT_MASK || (i & 0x9000) == 0x1000)
        return (info)(i | (I_VARIATION_SELECTOR_16 & ~I_VARIATION_SELECTOR_15 & ~I_VARIATION_SELECTOR_1_2_OR_3));
    return I_VARIATION_SELECTOR_16;
}
static info unset_emoji_presentation(info i) {
    if ((i & LIGATURE_TRANSPARENT_MASK) == LIGATURE_TRANSPARENT_MASK) return (info)(i & ~I_VARIATION_SELECTOR_16);
    return I_DEFAULT;
}
static bool is_text_presentation(info i) { return (i & I_VARIATION_SELECTOR_15) == I_VARIATION_SELECTOR_15; }
static info set_text_presentation(info i) {
    if ((i & LIGATURE_TRANSPARENT_MASK) == LIGATURE_TRANSPARENT_MASK)
        return (info)(i | (I_VARIATION_SELECTOR_15 & ~I_VARIATION_SELECTOR_16 & ~I_VARIATION_SELECTOR_1_2_OR_3));
    return I_VARIATION_SELECTOR_15;
}
static info unset_text_presentation(info i) { return (info)(i & ~I_VARIATION_SELECTOR_15); }
static bool is_vs1_2_3(info i) { return (i & I_VARIATION_SELECTOR_1_2_OR_3) == I_VARIATION_SELECTOR_1_2_OR_3; }
static info set_vs1_2_3(info i) {
    if ((i & LIGATURE_TRANSPARENT_MASK) == LIGATURE_TRANSPARENT_MASK)
        return (info)(i | (I_VARIATION_SELECTOR_1_2_OR_3 & ~I_VARIATION_SELECTOR_15 & ~I_VARIATION_SELECTOR_16));
    return I_VARIATION_SELECTOR_1_2_OR_3;
}
static info unset_vs1_2_3(info i) { return (info)(i & ~I_VARIATION_SELECTOR_1_2_OR_3); }

typedef struct {
    uint8_t width;
    info info;
} lookup;

static lookup lookup_width(uint32_t cp) {
    uint8_t t1 = TUI_UW_WIDTH_ROOT[cp >> 13];
    uint8_t t2 = TUI_UW_WIDTH_MIDDLE[t1][cp >> 7 & 0x3F];
    uint8_t packed = TUI_UW_WIDTH_LEAVES[t2][cp >> 2 & 0x1F];
    uint8_t width = packed >> (2 * (cp & 3)) & 3;
    if (width < 3) return (lookup){width, I_DEFAULT};
    if (cp == 0xA) return (lookup){1, I_LINE_FEED};
    if (cp == 0x5DC) return (lookup){1, I_HEBREW_LETTER_LAMED};
    if (cp >= 0x622 && cp <= 0x882) return (lookup){1, I_JOINING_GROUP_ALEF};
    if (cp >= 0x1780 && cp <= 0x17AF) return (lookup){1, I_KHMER_COENG_ELIGIBLE_LETTER};
    if (cp == 0x17D8) return (lookup){3, I_DEFAULT};
    if (cp == 0x1A10) return (lookup){1, I_BUGINESE_LETTER_YA};
    if (cp >= 0x2D31 && cp <= 0x2D6F) return (lookup){1, I_TIFINAGH_CONSONANT};
    if (cp >= 0xA4FC && cp <= 0xA4FD) return (lookup){1, I_LISU_TONE_LETTER_MYA_NA_JEU};
    if (cp == 0xFE01) return (lookup){0, I_VARIATION_SELECTOR_1_2_OR_3};
    if (cp == 0xFE0E) return (lookup){0, I_VARIATION_SELECTOR_15};
    if (cp == 0xFE0F) return (lookup){0, I_VARIATION_SELECTOR_16};
    if (cp == 0x10C03) return (lookup){1, I_OLD_TURKIC_LETTER_ORKHON_I};
    if (cp == 0x16D67) return (lookup){1, I_KIRAT_RAI_VOWEL_SIGN_E};
    if (cp == 0x16D68) return (lookup){1, I_KIRAT_RAI_VOWEL_SIGN_AI};
    if (cp >= 0x1F1E6 && cp <= 0x1F1FF) return (lookup){1, I_REGIONAL_INDICATOR};
    if (cp >= 0x1F3FB && cp <= 0x1F3FF) return (lookup){2, I_EMOJI_MODIFIER};
    return (lookup){2, I_EMOJI_PRESENTATION};
}

int tui_char_width(uint32_t cp) {
    if (cp < 0x7F) return cp >= 0x20 ? 1 : -1;
    if (cp >= 0xA0) return lookup_width(cp).width;
    return -1;
}

static bool is_transparent_zero_width(uint32_t cp) {
    if (lookup_width(cp).width != 0) return false;
    size_t lo = 0, hi = sizeof TUI_UW_NON_TRANSPARENT_ZERO_WIDTHS / sizeof TUI_UW_NON_TRANSPARENT_ZERO_WIDTHS[0];
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (cp < TUI_UW_NON_TRANSPARENT_ZERO_WIDTHS[mid][0]) hi = mid;
        else if (cp > TUI_UW_NON_TRANSPARENT_ZERO_WIDTHS[mid][1]) lo = mid + 1;
        else return false; /* found: not transparent */
    }
    return true;
}

static bool is_ligature_transparent(uint32_t c) {
    return c == 0x34F || (c >= 0x17B4 && c <= 0x17B5) || (c >= 0x180B && c <= 0x180D) || c == 0x180F ||
           c == 0x200D || (c >= 0xFE00 && c <= 0xFE0F) || (c >= 0xE0100 && c <= 0xE01EF);
}

static bool starts_emoji_presentation_seq(uint32_t cp) {
    int leaf;
    switch (cp >> 10) {
    case 0x0: leaf = 0; break;
    case 0x8: leaf = 1; break;
    case 0x9: leaf = 2; break;
    case 0xA: leaf = 3; break;
    case 0xC: leaf = 4; break;
    case 0x7C: leaf = 5; break;
    case 0x7D: leaf = 6; break;
    default: return false;
    }
    uint8_t byte = TUI_UW_EMOJI_PRESENTATION_LEAVES[leaf][(cp >> 3) & 0x7F];
    return ((byte >> (cp & 7)) & 1) == 1;
}

static bool in_pairs(const uint8_t (*leaf)[2], size_t n, uint8_t b) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (b < leaf[mid][0]) hi = mid;
        else if (b > leaf[mid][1]) lo = mid + 1;
        else return true;
    }
    return false;
}

#define LEAF(t) (t), (sizeof(t) / sizeof((t)[0]))

static bool starts_non_ideographic_text_presentation_seq(uint32_t cp) {
    uint8_t b = (uint8_t)(cp & 0xFF);
    switch (cp >> 8) {
    case 0x23: return in_pairs(LEAF(TUI_UW_TEXT_PRESENTATION_LEAF_0), b);
    case 0x25: return in_pairs(LEAF(TUI_UW_TEXT_PRESENTATION_LEAF_1), b);
    case 0x26: return in_pairs(LEAF(TUI_UW_TEXT_PRESENTATION_LEAF_2), b);
    case 0x27: return in_pairs(LEAF(TUI_UW_TEXT_PRESENTATION_LEAF_3), b);
    case 0x2B: return in_pairs(LEAF(TUI_UW_TEXT_PRESENTATION_LEAF_4), b);
    case 0x1F0: return in_pairs(LEAF(TUI_UW_TEXT_PRESENTATION_LEAF_5), b);
    case 0x1F3: return in_pairs(LEAF(TUI_UW_TEXT_PRESENTATION_LEAF_6), b);
    case 0x1F4: return in_pairs(LEAF(TUI_UW_TEXT_PRESENTATION_LEAF_7), b);
    case 0x1F5: return in_pairs(LEAF(TUI_UW_TEXT_PRESENTATION_LEAF_8), b);
    case 0x1F6: return in_pairs(LEAF(TUI_UW_TEXT_PRESENTATION_LEAF_9), b);
    default: return false;
    }
}

static bool is_emoji_modifier_base(uint32_t cp) {
    uint8_t b = (uint8_t)(cp & 0xFF);
    switch (cp >> 8) {
    case 0x26: return in_pairs(LEAF(TUI_UW_EMOJI_MODIFIER_LEAF_0), b);
    case 0x27: return in_pairs(LEAF(TUI_UW_EMOJI_MODIFIER_LEAF_1), b);
    case 0x1F3: return in_pairs(LEAF(TUI_UW_EMOJI_MODIFIER_LEAF_2), b);
    case 0x1F4: return in_pairs(LEAF(TUI_UW_EMOJI_MODIFIER_LEAF_3), b);
    case 0x1F5: return in_pairs(LEAF(TUI_UW_EMOJI_MODIFIER_LEAF_4), b);
    case 0x1F6: return in_pairs(LEAF(TUI_UW_EMOJI_MODIFIER_LEAF_5), b);
    case 0x1F9: return in_pairs(LEAF(TUI_UW_EMOJI_MODIFIER_LEAF_6), b);
    case 0x1FA: return in_pairs(LEAF(TUI_UW_EMOJI_MODIFIER_LEAF_7), b);
    default: return false;
    }
}

typedef struct {
    int width;
    info info;
} step;

static bool ri(uint32_t c) { return c >= 0x1F1E6 && c <= 0x1F1FF; }
static bool tag_letter(uint32_t c) { return c >= 0xE0061 && c <= 0xE007A; }
static bool tag_digit(uint32_t c) { return c >= 0xE0030 && c <= 0xE0039; }

static step width_in_str(uint32_t c, info next) {
    if (is_emoji_presentation(next)) {
        if (starts_emoji_presentation_seq(c)) return (step){is_zwj_emoji_presentation(next) ? 0 : 2, I_EMOJI_PRESENTATION};
        next = unset_emoji_presentation(next);
    }
    if (c <= 0xA0) {
        if (c == '\n') return (step){1, I_LINE_FEED};
        if (c == '\r' && next == I_LINE_FEED) return (step){0, I_DEFAULT};
        return (step){1, I_DEFAULT};
    }
    if (next != I_DEFAULT) {
        if (c == 0xFE0F) return (step){0, set_emoji_presentation(next)};
        if (c == 0xFE01) return (step){0, set_vs1_2_3(next)};
        if (c == 0xFE0E) return (step){0, set_text_presentation(next)};
        if (is_text_presentation(next)) {
            if (starts_non_ideographic_text_presentation_seq(c)) return (step){1, I_DEFAULT};
            next = unset_text_presentation(next);
        } else if (is_vs1_2_3(next)) {
            if (c == 0x2018 || c == 0x2019 || c == 0x201C || c == 0x201D) return (step){2, I_DEFAULT};
            next = unset_vs1_2_3(next);
        }
        if (is_ligature_transparent_info(next)) {
            if (c == 0x200D) return (step){0, set_zwj_bit(next)};
            if (is_ligature_transparent(c)) return (step){0, next};
        }
        switch (next) {
        case I_JOINING_GROUP_ALEF:
            if (c == 0x644 || (c >= 0x6B5 && c <= 0x6B8) || c == 0x76A || c == 0x8A6 || c == 0x8C7)
                return (step){0, I_DEFAULT};
            if (is_transparent_zero_width(c)) return (step){0, I_JOINING_GROUP_ALEF};
            break;
        case I_ZWJ_HEBREW_LETTER_LAMED:
            if (c == 0x05D0) return (step){0, I_DEFAULT};
            break;
        case I_KHMER_COENG_ELIGIBLE_LETTER:
            if (c == 0x17D2) return (step){-1, I_DEFAULT};
            break;
        case I_ZWJ_BUGINESE_LETTER_YA:
            if (c == 0x1A17) return (step){0, I_BUGINESE_VOWEL_SIGN_I_ZWJ_LETTER_YA};
            break;
        case I_BUGINESE_VOWEL_SIGN_I_ZWJ_LETTER_YA:
            if (c == 0x1A15) return (step){0, I_DEFAULT};
            break;
        default: break;
        }
        if ((next == I_TIFINAGH_CONSONANT || next == I_ZWJ_TIFINAGH_CONSONANT) && c == 0x2D7F)
            return (step){1, I_TIFINAGH_JOINER_CONSONANT};
        if (next == I_ZWJ_TIFINAGH_CONSONANT && ((c >= 0x2D31 && c <= 0x2D65) || c == 0x2D6F)) return (step){0, I_DEFAULT};
        if (next == I_TIFINAGH_JOINER_CONSONANT && ((c >= 0x2D31 && c <= 0x2D65) || c == 0x2D6F))
            return (step){-1, I_DEFAULT};
        if (next == I_LISU_TONE_LETTER_MYA_NA_JEU && c >= 0xA4F8 && c <= 0xA4FB) return (step){0, I_DEFAULT};
        if (next == I_ZWJ_OLD_TURKIC_LETTER_ORKHON_I && c == 0x10C32) return (step){0, I_DEFAULT};
        if (next == I_EMOJI_MODIFIER && is_emoji_modifier_base(c)) return (step){0, I_EMOJI_PRESENTATION};
        if ((next == I_REGIONAL_INDICATOR || next == I_SEVERAL_REGIONAL_INDICATOR) && ri(c))
            return (step){1, I_SEVERAL_REGIONAL_INDICATOR};
        if ((next == I_EMOJI_PRESENTATION || next == I_SEVERAL_REGIONAL_INDICATOR ||
             next == I_EVEN_REGIONAL_INDICATOR_ZWJ_PRESENTATION || next == I_ODD_REGIONAL_INDICATOR_ZWJ_PRESENTATION ||
             next == I_EMOJI_MODIFIER) &&
            c == 0x200D)
            return (step){0, I_ZWJ_EMOJI_PRESENTATION};
        if (next == I_ZWJ_EMOJI_PRESENTATION && c == 0x20E3) return (step){0, I_KEYCAP_ZWJ_EMOJI_PRESENTATION};
        if (next == I_VS16_ZWJ_EMOJI_PRESENTATION && starts_emoji_presentation_seq(c)) return (step){0, I_EMOJI_PRESENTATION};
        if (next == I_VS16_KEYCAP_ZWJ_EMOJI_PRESENTATION && ((c >= '0' && c <= '9') || c == '#' || c == '*'))
            return (step){0, I_EMOJI_PRESENTATION};
        if (next == I_ZWJ_EMOJI_PRESENTATION && ri(c)) return (step){1, I_REGIONAL_INDICATOR_ZWJ_PRESENTATION};
        if ((next == I_REGIONAL_INDICATOR_ZWJ_PRESENTATION || next == I_ODD_REGIONAL_INDICATOR_ZWJ_PRESENTATION) && ri(c))
            return (step){-1, I_EVEN_REGIONAL_INDICATOR_ZWJ_PRESENTATION};
        if (next == I_EVEN_REGIONAL_INDICATOR_ZWJ_PRESENTATION && ri(c))
            return (step){3, I_ODD_REGIONAL_INDICATOR_ZWJ_PRESENTATION};
        if (next == I_ZWJ_EMOJI_PRESENTATION && c >= 0x1F3FB && c <= 0x1F3FF) return (step){0, I_EMOJI_MODIFIER};
        if (next == I_ZWJ_EMOJI_PRESENTATION && c == 0xE007F) return (step){0, I_TAG_END_ZWJ_EMOJI_PRESENTATION};
        if (next == I_TAG_END_ZWJ_EMOJI_PRESENTATION && tag_letter(c)) return (step){0, I_TAG_A1_END_ZWJ_EMOJI_PRESENTATION};
        if (next == I_TAG_A1_END_ZWJ_EMOJI_PRESENTATION && tag_letter(c)) return (step){0, I_TAG_A2_END_ZWJ_EMOJI_PRESENTATION};
        if (next == I_TAG_A2_END_ZWJ_EMOJI_PRESENTATION && tag_letter(c)) return (step){0, I_TAG_A3_END_ZWJ_EMOJI_PRESENTATION};
        if (next == I_TAG_A3_END_ZWJ_EMOJI_PRESENTATION && tag_letter(c)) return (step){0, I_TAG_A4_END_ZWJ_EMOJI_PRESENTATION};
        if (next == I_TAG_A4_END_ZWJ_EMOJI_PRESENTATION && tag_letter(c)) return (step){0, I_TAG_A5_END_ZWJ_EMOJI_PRESENTATION};
        if (next == I_TAG_A5_END_ZWJ_EMOJI_PRESENTATION && tag_letter(c)) return (step){0, I_TAG_A6_END_ZWJ_EMOJI_PRESENTATION};
        if ((next == I_TAG_END_ZWJ_EMOJI_PRESENTATION || next == I_TAG_A1_END_ZWJ_EMOJI_PRESENTATION ||
             next == I_TAG_A2_END_ZWJ_EMOJI_PRESENTATION || next == I_TAG_A3_END_ZWJ_EMOJI_PRESENTATION ||
             next == I_TAG_A4_END_ZWJ_EMOJI_PRESENTATION) &&
            tag_digit(c))
            return (step){0, I_TAG_D1_END_ZWJ_EMOJI_PRESENTATION};
        if (next == I_TAG_D1_END_ZWJ_EMOJI_PRESENTATION && tag_digit(c)) return (step){0, I_TAG_D2_END_ZWJ_EMOJI_PRESENTATION};
        if (next == I_TAG_D2_END_ZWJ_EMOJI_PRESENTATION && tag_digit(c)) return (step){0, I_TAG_D3_END_ZWJ_EMOJI_PRESENTATION};
        if ((next == I_TAG_A3_END_ZWJ_EMOJI_PRESENTATION || next == I_TAG_A4_END_ZWJ_EMOJI_PRESENTATION ||
             next == I_TAG_A5_END_ZWJ_EMOJI_PRESENTATION || next == I_TAG_A6_END_ZWJ_EMOJI_PRESENTATION ||
             next == I_TAG_D3_END_ZWJ_EMOJI_PRESENTATION) &&
            c == 0x1F3F4)
            return (step){0, I_EMOJI_PRESENTATION};
        if (next == I_ZWJ_EMOJI_PRESENTATION && lookup_width(c).info == I_EMOJI_PRESENTATION)
            return (step){0, I_EMOJI_PRESENTATION};
        if (next == I_KIRAT_RAI_VOWEL_SIGN_E && c == 0x16D63) return (step){0, I_DEFAULT};
        if (next == I_KIRAT_RAI_VOWEL_SIGN_E && c == 0x16D67) return (step){0, I_KIRAT_RAI_VOWEL_SIGN_AI};
        if (next == I_KIRAT_RAI_VOWEL_SIGN_E && c == 0x16D68) return (step){1, I_KIRAT_RAI_VOWEL_SIGN_E};
        if (next == I_KIRAT_RAI_VOWEL_SIGN_E && c == 0x16D69) return (step){0, I_DEFAULT};
        if (next == I_KIRAT_RAI_VOWEL_SIGN_AI && c == 0x16D63) return (step){0, I_DEFAULT};
    }
    lookup l = lookup_width(c);
    return (step){l.width, l.info};
}

size_t tui_str_width(const char *s, size_t len) {
    /* Decode forwards, fold backwards. */
    uint32_t small[256];
    uint32_t *cps = small;
    size_t n = 0, cap = 256;
    for (size_t i = 0; i < len;) {
        if (n == cap) {
            cap *= 2;
            uint32_t *bigger = malloc(cap * sizeof *bigger);
            if (!bigger) abort();
            for (size_t k = 0; k < n; k++) bigger[k] = cps[k];
            if (cps != small) free(cps);
            cps = bigger;
        }
        cps[n++] = tui_utf8_next(s, len, &i);
    }
    size_t sum = 0;
    info next = I_DEFAULT;
    for (size_t k = n; k-- > 0;) {
        step st = width_in_str(cps[k], next);
        sum += (size_t)(ptrdiff_t)st.width; /* wrapping_add_signed */
        next = st.info;
    }
    if (cps != small) free(cps);
    return sum;
}

size_t tui_cstr_width(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return tui_str_width(s, len);
}
