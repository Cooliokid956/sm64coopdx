#ifdef MC_ENUM
#define MOD_CATEGORY_DEF(key) MOD_CATEGORY_ ## key,
#define MOD_CATEGORY(key, category)
#else
#define MOD_CATEGORY_DEF(key) { #key, NULL },
#define MOD_CATEGORY(key, category) { #key, category },
#endif

MOD_CATEGORY_DEF(ALL)
MOD_CATEGORY_DEF(ENABLED)
MOD_CATEGORY_DEF(MISC)
MOD_CATEGORY(ROMHACKS, "romhack")
MOD_CATEGORY(GAMEMODES, "gamemode")
MOD_CATEGORY(MOVESETS, "moveset")
MOD_CATEGORY(GRAPHICS, "graphics")
MOD_CATEGORY(QOL, "qol")
MOD_CATEGORY(UTILITY, "utility")
MOD_CATEGORY(AUDIO, "audio")
MOD_CATEGORY(CHARACTERS, "character")

#undef MOD_CATEGORY_DEF
#undef MOD_CATEGORY