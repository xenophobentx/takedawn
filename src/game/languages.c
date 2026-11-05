/* languages.c - the Language row's languages (languages.h). */
#include "languages.h"

#include <stdlib.h>
#include <string.h>

static const char *const k_names[BO3_LANG_COUNT] = {
    "english", "japanese", "german", "french", "spanish", "italian",
    "korean", "chinese", "portuguese", "dutch", "swedish",
};

/* Each choice's text file(s); English is GlobalUs on the NTSC-U disc and
 * GlobalEn on the Europe/Australia one. */
static const char *const k_text[BO3_LANG_COUNT][2] = {
    { "Data/GlobalUs.bin", "Data/GlobalEn.bin" }, { "Data/GlobalJa.bin", NULL },
    { "Data/GlobalGe.bin", NULL }, { "Data/GlobalFr.bin", NULL },
    { "Data/GlobalSp.bin", NULL }, { "Data/GlobalIt.bin", NULL },
    { NULL, NULL }, { NULL, NULL }, { NULL, NULL },
    { "Data/GlobalDu.bin", NULL }, { "Data/GlobalSw.bin", NULL },
};

unsigned bo3_language_mask(bo3_lang_has_fn has, void *ctx, int title_set)
{
    unsigned mask = 0;
    int i, j;

    for (i = 0; i < BO3_LANG_COUNT; i++) {
        if ((i == BO3_LANG_DUTCH || i == BO3_LANG_SWEDISH) && !title_set)
            continue;
        for (j = 0; j < 2; j++)
            if (k_text[i][j] && has(k_text[i][j], ctx))
                mask |= 1u << i;
    }
    return mask;
}

int bo3_language_index(const char *setting)
{
    char *end;
    long n;
    int i;

    if (!setting || !*setting)
        return -1;
    for (i = 0; i < BO3_LANG_COUNT; i++)
        if (!strcmp(setting, k_names[i]))
            return i;
    n = strtol(setting, &end, 10);
    return !*end && n >= 1 && n <= 9 ? (int)n - 1 : -1;
}

const char *bo3_language_name(int index)
{
    return index >= 0 && index < BO3_LANG_COUNT ? k_names[index] : "?";
}
