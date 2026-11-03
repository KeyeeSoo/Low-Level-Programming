#ifndef ART_STYLE_H
#define ART_STYLE_H

/* Private palette and layout shared by drawing modules. */
/* ---- Palette ------------------------------------------------------------ */
#define INK 0x2B2233u
#define SKY_TOP 0x17142Eu
#define SKY_BOTTOM 0x2A2547u
#define PARCHMENT 0xF7DFB4u
#define CREAM 0xFBEBCFu
#define WOOD_DARK 0x2E2019u
#define WOOD 0x5A3F2Fu
#define WOOD_LIGHT 0x7B5A41u
#define FLOOR 0x4C372Bu
#define PLATE 0xF3E2C2u
#define CARD_MINT 0xBFE3CFu
#define CARD_BLUE 0xDCE6F5u
#define CARD_SAGE 0xDDE3DAu
#define CARD_PINK 0xF9C6CDu
#define LILAC 0xC9B7EEu
#define POTION 0x79D44Fu

/* Attic bounds. These match the walls enforced by game_move() on the server. */
#define ATTIC_X 199.0f
#define ATTIC_Y 104.0f
#define ATTIC_W 588.0f
#define ATTIC_H 482.0f
#define WALL_H 48.0f
#define LEFT_COL_X 8.0f
#define LEFT_COL_W 182.0f
#define RECIPE_Y 104.0f
#define RECIPE_H 238.0f
#define MINI_Y 350.0f
#define MINI_H 388.0f
#define RUNE_CARD_Y 416.0f
#define STIR_CARD_Y 586.0f
#define PREVIEW_CARD_H 138.0f
#define PREVIEW_CARD_X (LEFT_COL_X + 12.0f)
#define PREVIEW_CARD_W (LEFT_COL_W - 24.0f)

#define SETTINGS_CARD_X 200.0f
#define SETTINGS_CARD_Y 262.0f
#define SETTINGS_CARD_W 400.0f
#define SETTINGS_CARD_H 310.0f
#define SETTINGS_ROW_X (SETTINGS_CARD_X + 24.0f)
#define SETTINGS_OPTION_W 112.0f
#define SETTINGS_OPTION_H 36.0f
#define SETTINGS_OPTION_GAP 8.0f
#define SETTINGS_SIZE_Y (SETTINGS_CARD_Y + 94.0f)
#define SETTINGS_ANIM_Y (SETTINGS_CARD_Y + 178.0f)
#define SETTINGS_BACK_W 120.0f
#define SETTINGS_BACK_Y (SETTINGS_CARD_Y + 244.0f)



#define CHAT_X 393.0f
#define CHAT_Y 628.0f
#define CHAT_W 237.0f
#define CHAT_H 110.0f
#define CHAT_INPUT_Y (CHAT_Y + CHAT_H - 26.0f)


#define MENU_IMAGE_FILE "assets/main_menu.png"
#define MENU_IMAGE_W 972.0f
#define MENU_IMAGE_H 1090.0f
#define MENU_CROP_TOP 110.0f
#define MENU_SCALE ((float)CANVAS_W / MENU_IMAGE_W)
#define MENU_X(px) ((px) * MENU_SCALE)
#define MENU_Y(py) (((py) - MENU_CROP_TOP) * MENU_SCALE)

/* Where the wooden signs are painted: x, y, width, height in painting pixels. */
#define MENU_BUTTON_COUNT 3


#define LOGIN_IMAGE_FILE "assets/login.png"
#define LOGIN_IMAGE_W 970.0f
#define LOGIN_IMAGE_H 1092.0f
#define LOGIN_CROP_TOP 90.0f
#define LOGIN_SCALE ((float)CANVAS_W / LOGIN_IMAGE_W)
#define LOGIN_X(px) ((px) * LOGIN_SCALE)
#define LOGIN_Y(py) (((py) - LOGIN_CROP_TOP) * LOGIN_SCALE)
#define LOGIN_PILL_COLOR 0xF0DED2u
#define LOGIN_PAPER_COLOR 0xF9E0CCu

/* Everything clickable on the form: x, y, width, height in painting pixels. */


#define PLAY_AGAIN_W 170.0f
#define PLAY_AGAIN_H 40.0f
#define PLAY_AGAIN_X (ATTIC_X + ATTIC_W * 0.5f - PLAY_AGAIN_W * 0.5f)
#define PLAY_AGAIN_Y (ATTIC_Y + ATTIC_H * 0.5f + 28.0f)

/* Gameplay Exit button, in logical canvas coordinates. */
#define EXIT_BUTTON_X 700.0f
#define EXIT_BUTTON_Y 8.0f
#define EXIT_BUTTON_W 86.0f
#define EXIT_BUTTON_H 30.0f

#define PI 3.14159265f

#endif
