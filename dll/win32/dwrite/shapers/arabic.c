/*
 * Copyright HarfBuzz Project authors
 * Copyright 2020 Nikolay Sivov for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "dwrite_private.h"
#include "scripts.h"

static const unsigned int arabic_features[] =
{
    DWRITE_MAKE_OPENTYPE_TAG('i','s','o','l'),
    DWRITE_MAKE_OPENTYPE_TAG('f','i','n','a'),
    DWRITE_MAKE_OPENTYPE_TAG('f','i','n','2'),
    DWRITE_MAKE_OPENTYPE_TAG('f','i','n','3'),
    DWRITE_MAKE_OPENTYPE_TAG('m','e','d','i'),
    DWRITE_MAKE_OPENTYPE_TAG('m','e','d','2'),
    DWRITE_MAKE_OPENTYPE_TAG('i','n','i','t'),
};

enum arabic_shaping_action
{
    ISOL,
    FINA,
    FIN2,
    FIN3,
    MEDI,
    MED2,
    INIT,
    NONE,
    NUM_FEATURES = NONE,
};

static BOOL feature_is_syriac(unsigned int tag)
{
    return tag == arabic_features[FIN2] || tag == arabic_features[FIN3] ||
            tag == arabic_features[MED2];
}

static void arabic_collect_features(struct scriptshaping_context *context,
        struct shaping_features *features)
{
    unsigned int i;

    shape_enable_feature(features, DWRITE_MAKE_OPENTYPE_TAG('c','c','m','p'), 0);
    shape_enable_feature(features, DWRITE_MAKE_OPENTYPE_TAG('l','o','c','l'), 0);
    shape_start_next_stage(features, NULL);

    for (i = 0; i < ARRAY_SIZE(arabic_features); ++i)
    {
        unsigned int flags = context->script == Script_Arabic && !feature_is_syriac(arabic_features[i]) ?
                FEATURE_HAS_FALLBACK : 0;
        shape_add_feature_full(features, arabic_features[i], flags, 1);
        shape_start_next_stage(features, NULL);
    }

    shape_enable_feature(features, DWRITE_MAKE_OPENTYPE_TAG('r','l','i','g'), FEATURE_MANUAL_ZWJ | FEATURE_HAS_FALLBACK);

    shape_enable_feature(features, DWRITE_MAKE_OPENTYPE_TAG('r','c','l','t'), FEATURE_MANUAL_ZWJ);
    shape_enable_feature(features, DWRITE_MAKE_OPENTYPE_TAG('c','a','l','t'), FEATURE_MANUAL_ZWJ);
    shape_start_next_stage(features, NULL);

    shape_enable_feature(features, DWRITE_MAKE_OPENTYPE_TAG('m','s','e','t'), 0);
}

enum arabic_joining_type
{
    JOINING_TYPE_U = 0,
    JOINING_TYPE_L = 1,
    JOINING_TYPE_R = 2,
    JOINING_TYPE_D = 3,
    JOINING_TYPE_C = JOINING_TYPE_D,
    JOINING_GROUP_ALAPH = 4,
    JOINING_GROUP_DALATH_RISH = 5,
    JOINING_TYPES = 6,
    JOINING_TYPE_T = 6,
};

static const struct arabic_state_table_entry
{
    unsigned char prev_action;
    unsigned char curr_action;
    unsigned char next_state;
}
arabic_state_table[][JOINING_TYPES] =
{
    /*     U,              L,             R,             D,           ALAPH,      DALATH_RISH  */
    /* State 0: prev was U, not willing to join. */
    { {NONE,NONE,0}, {NONE,ISOL,2}, {NONE,ISOL,1}, {NONE,ISOL,2}, {NONE,ISOL,1}, {NONE,ISOL,6}, },

    /* State 1: prev was R or ISOL/ALAPH, not willing to join. */
    { {NONE,NONE,0}, {NONE,ISOL,2}, {NONE,ISOL,1}, {NONE,ISOL,2}, {NONE,FIN2,5}, {NONE,ISOL,6}, },

    /* State 2: prev was D/L in ISOL form, willing to join. */
    { {NONE,NONE,0}, {NONE,ISOL,2}, {INIT,FINA,1}, {INIT,FINA,3}, {INIT,FINA,4}, {INIT,FINA,6}, },

    /* State 3: prev was D in FINA form, willing to join. */
    { {NONE,NONE,0}, {NONE,ISOL,2}, {MEDI,FINA,1}, {MEDI,FINA,3}, {MEDI,FINA,4}, {MEDI,FINA,6}, },

    /* State 4: prev was FINA ALAPH, not willing to join. */
    { {NONE,NONE,0}, {NONE,ISOL,2}, {MED2,ISOL,1}, {MED2,ISOL,2}, {MED2,FIN2,5}, {MED2,ISOL,6}, },

    /* State 5: prev was FIN2/FIN3 ALAPH, not willing to join. */
    { {NONE,NONE,0}, {NONE,ISOL,2}, {ISOL,ISOL,1}, {ISOL,ISOL,2}, {ISOL,FIN2,5}, {ISOL,ISOL,6}, },

    /* State 6: prev was DALATH/RISH, not willing to join. */
    { {NONE,NONE,0}, {NONE,ISOL,2}, {NONE,ISOL,1}, {NONE,ISOL,2}, {NONE,FIN3,5}, {NONE,ISOL,6}, }
};

extern const unsigned short arabic_shaping_table[];

static unsigned short arabic_get_joining_type(UINT ch)
{
    return get_table_entry_32(arabic_shaping_table, ch);
}

static void arabic_set_shaping_action(struct scriptshaping_context *context,
        unsigned int idx, enum arabic_shaping_action action)
{
    context->glyph_infos[idx].props &= ~(0xf << 16);
    context->glyph_infos[idx].props |= (action & 0xf) << 16;
}

static enum arabic_shaping_action arabic_get_shaping_action(const struct scriptshaping_context *context,
        unsigned int idx)
{
    return (context->glyph_infos[idx].props >> 16) & 0xf;
}

static void arabic_set_joining_actions(struct scriptshaping_context *context, BOOL reverse)
{
    unsigned int i, prev = ~0u, state = 0;

    for (i = 0; i < context->glyph_count; ++i)
    {
        unsigned int idx = reverse ? context->glyph_count - i - 1 : i;
        unsigned short this_type = arabic_get_joining_type(context->glyph_infos[idx].codepoint);
        const struct arabic_state_table_entry *entry;

        if (this_type == JOINING_TYPE_T)
        {
            arabic_set_shaping_action(context, idx, NONE);
            continue;
        }

        entry = &arabic_state_table[state][this_type];

        if (entry->prev_action != NONE && prev != ~0u)
            arabic_set_shaping_action(context, prev, entry->prev_action);

        arabic_set_shaping_action(context, idx, entry->curr_action);

        prev = idx;
        state = entry->next_state;
    }
}

static const struct arabic_justification
{
    unsigned short first;
    unsigned short last;
    unsigned char init;
    unsigned char medi;
    unsigned char fina;
}
arabic_justification_table[] =
{
    { 0x0622, 0x0623, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_ALEF, SCRIPT_JUSTIFY_ARABIC_ALEF },
    { 0x0624, 0x0624, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_RA, SCRIPT_JUSTIFY_ARABIC_RA },
    { 0x0625, 0x0625, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_ALEF, SCRIPT_JUSTIFY_ARABIC_ALEF },
    { 0x0626, 0x0626, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_BA, SCRIPT_JUSTIFY_ARABIC_BARA },
    { 0x0627, 0x0627, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_ALEF, SCRIPT_JUSTIFY_ARABIC_ALEF },
    { 0x0628, 0x0628, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_BA, SCRIPT_JUSTIFY_ARABIC_ALEF },
    { 0x0629, 0x0629, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_HA, SCRIPT_JUSTIFY_ARABIC_HA },
    { 0x062a, 0x062b, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_BA, SCRIPT_JUSTIFY_ARABIC_ALEF },
    { 0x062c, 0x062e, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_NORMAL, SCRIPT_JUSTIFY_ARABIC_NORMAL },
    { 0x062f, 0x0630, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_HA, SCRIPT_JUSTIFY_ARABIC_HA },
    { 0x0631, 0x0632, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_BARA, SCRIPT_JUSTIFY_ARABIC_BARA },
    { 0x0633, 0x0636, SCRIPT_JUSTIFY_ARABIC_SEEN, SCRIPT_JUSTIFY_ARABIC_SEEN_M, SCRIPT_JUSTIFY_ARABIC_NORMAL },
    { 0x0637, 0x0638, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_NORMAL, SCRIPT_JUSTIFY_ARABIC_NORMAL },
    { 0x0639, 0x063a, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_NORMAL, SCRIPT_JUSTIFY_ARABIC_RA },
    { 0x0641, 0x0642, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_NORMAL, SCRIPT_JUSTIFY_ARABIC_RA },
    { 0x0643, 0x0644, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_NORMAL, SCRIPT_JUSTIFY_ARABIC_ALEF },
    { 0x0645, 0x0645, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_NORMAL, SCRIPT_JUSTIFY_ARABIC_NORMAL },
    { 0x0646, 0x0646, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_BA, SCRIPT_JUSTIFY_ARABIC_NORMAL },
    { 0x0647, 0x0647, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_NORMAL, SCRIPT_JUSTIFY_ARABIC_HA },
    { 0x0648, 0x0648, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_RA, SCRIPT_JUSTIFY_ARABIC_RA },
    { 0x0649, 0x0649, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_BARA },
    { 0x064a, 0x064a, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_BA, SCRIPT_JUSTIFY_ARABIC_BARA },
    { 0x066e, 0x066e, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_BA, SCRIPT_JUSTIFY_ARABIC_ALEF },
    { 0x066f, 0x066f, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_NORMAL, SCRIPT_JUSTIFY_ARABIC_RA },
    { 0x0671, 0x0673, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_ALEF, SCRIPT_JUSTIFY_ARABIC_ALEF },
    { 0x0675, 0x0675, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_ALEF, SCRIPT_JUSTIFY_ARABIC_ALEF },
    { 0x0676, 0x0677, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_RA, SCRIPT_JUSTIFY_ARABIC_RA },
    { 0x0678, 0x0680, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_BA, SCRIPT_JUSTIFY_ARABIC_ALEF },
    { 0x0681, 0x0687, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_NORMAL, SCRIPT_JUSTIFY_ARABIC_NORMAL },
    { 0x0688, 0x068f, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_HA, SCRIPT_JUSTIFY_ARABIC_HA },
    { 0x0690, 0x0690, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_NORMAL, SCRIPT_JUSTIFY_ARABIC_NORMAL },
    { 0x0691, 0x0699, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_BARA, SCRIPT_JUSTIFY_ARABIC_BARA },
    { 0x069a, 0x069e, SCRIPT_JUSTIFY_ARABIC_SEEN, SCRIPT_JUSTIFY_ARABIC_SEEN_M, SCRIPT_JUSTIFY_NONE },
    { 0x069f, 0x069f, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_NORMAL, SCRIPT_JUSTIFY_ARABIC_NORMAL },
    { 0x06a0, 0x06a8, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_NORMAL, SCRIPT_JUSTIFY_ARABIC_RA },
    { 0x06a9, 0x06af, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_NORMAL, SCRIPT_JUSTIFY_ARABIC_ALEF },
    { 0x06b0, 0x06b4, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_ALEF },
    { 0x06b5, 0x06b8, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_NORMAL, SCRIPT_JUSTIFY_ARABIC_ALEF },
    { 0x06b9, 0x06bd, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_BA, SCRIPT_JUSTIFY_ARABIC_NORMAL },
    { 0x06be, 0x06be, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_NORMAL, SCRIPT_JUSTIFY_ARABIC_HA },
    { 0x06bf, 0x06bf, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_NORMAL, SCRIPT_JUSTIFY_ARABIC_NORMAL },
    { 0x06c0, 0x06c0, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_HA, SCRIPT_JUSTIFY_ARABIC_HA },
    { 0x06c1, 0x06c1, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_NORMAL, SCRIPT_JUSTIFY_ARABIC_HA },
    { 0x06c2, 0x06c2, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_HA },
    { 0x06c3, 0x06c3, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_HA, SCRIPT_JUSTIFY_ARABIC_HA },
    { 0x06c4, 0x06cb, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_RA, SCRIPT_JUSTIFY_ARABIC_RA },
    { 0x06cc, 0x06cc, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_BA, SCRIPT_JUSTIFY_ARABIC_BARA },
    { 0x06cd, 0x06cd, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_BARA, SCRIPT_JUSTIFY_ARABIC_BARA },
    { 0x06ce, 0x06ce, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_BA, SCRIPT_JUSTIFY_ARABIC_BARA },
    { 0x06cf, 0x06cf, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_RA, SCRIPT_JUSTIFY_ARABIC_RA },
    { 0x06d0, 0x06d1, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_BA, SCRIPT_JUSTIFY_ARABIC_BARA },
    { 0x06d2, 0x06d3, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_NORMAL, SCRIPT_JUSTIFY_ARABIC_NORMAL },
    { 0x06fa, 0x06fb, SCRIPT_JUSTIFY_ARABIC_SEEN, SCRIPT_JUSTIFY_ARABIC_SEEN_M, SCRIPT_JUSTIFY_ARABIC_NORMAL },
    { 0x06fc, 0x06fc, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_NORMAL, SCRIPT_JUSTIFY_ARABIC_RA },
    { 0x0750, 0x0756, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_BA, SCRIPT_JUSTIFY_ARABIC_NORMAL },
    { 0x0757, 0x0757, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_NORMAL, SCRIPT_JUSTIFY_ARABIC_NORMAL },
    { 0x0758, 0x0758, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_NORMAL, SCRIPT_JUSTIFY_NONE },
    { 0x0759, 0x075a, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_HA, SCRIPT_JUSTIFY_ARABIC_HA },
    { 0x075b, 0x075b, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_RA, SCRIPT_JUSTIFY_ARABIC_RA },
    { 0x075c, 0x075c, SCRIPT_JUSTIFY_ARABIC_SEEN, SCRIPT_JUSTIFY_ARABIC_SEEN_M, SCRIPT_JUSTIFY_NONE },
    { 0x075d, 0x075f, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_NORMAL, SCRIPT_JUSTIFY_NONE },
    { 0x0760, 0x076a, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_NORMAL, SCRIPT_JUSTIFY_ARABIC_NORMAL },
    { 0x076b, 0x076c, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_RA, SCRIPT_JUSTIFY_ARABIC_RA },
    { 0x076d, 0x076d, SCRIPT_JUSTIFY_ARABIC_SEEN, SCRIPT_JUSTIFY_ARABIC_SEEN_M, SCRIPT_JUSTIFY_ARABIC_NORMAL },
};

static unsigned char arabic_get_justification(unsigned int ch, enum arabic_shaping_action action)
{
    unsigned int i;

    if (ch == 0x640)
        return SCRIPT_JUSTIFY_ARABIC_KASHIDA;
    if (ch == 0x20)
        return SCRIPT_JUSTIFY_ARABIC_BLANK;

    for (i = 0; i < ARRAY_SIZE(arabic_justification_table); ++i)
    {
        const struct arabic_justification *entry = &arabic_justification_table[i];

        if (ch < entry->first || ch > entry->last)
            continue;
        if (action == INIT)
            return entry->init;
        if (action == MEDI)
            return entry->medi;
        if (action == FINA)
            return entry->fina;
        break;
    }

    return SCRIPT_JUSTIFY_NONE;
}

static void arabic_set_justification(struct scriptshaping_context *context)
{
    unsigned int i;

    arabic_set_joining_actions(context, !context->is_rtl);

    for (i = 0; i < context->glyph_count; ++i)
    {
        context->u.buffer.glyph_props[i].justification = arabic_get_justification(context->glyph_infos[i].codepoint,
                arabic_get_shaping_action(context, i));
    }
}

static void arabic_setup_masks(struct scriptshaping_context *context,
        const struct shaping_features *features)
{
    unsigned int i;
    unsigned int masks[NUM_FEATURES+1];

    if (context->script == Script_Arabic)
        arabic_set_justification(context);

    arabic_set_joining_actions(context, FALSE);

    for (i = 0; i < NUM_FEATURES; ++i)
        masks[i] = shape_get_feature_1_mask(features, arabic_features[i]);
    masks[NONE] = 0;

    /* Unaffected glyphs get action NONE with zero mask. */
    for (i = 0; i < context->glyph_count; ++i)
    {
        enum arabic_shaping_action action = arabic_get_shaping_action(context, i);
        if (action != NONE)
            opentype_layout_unsafe_to_break(context, i, i + 1);
        context->glyph_infos[i].mask |= masks[action];
    }
}

const struct shaper arabic_shaper =
{
    .collect_features = arabic_collect_features,
    .setup_masks = arabic_setup_masks,
};
