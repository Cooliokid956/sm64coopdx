--- @type integer
FONT_TINY = -1

--- @type integer
ANIM_FLAG_FORWARD = (1 << 1)

SPECIAL_WARP_CAKE             = -1 --- @type integer
SPECIAL_WARP_GODDARD          = -2 --- @type integer
SPECIAL_WARP_GODDARD_GAMEOVER = -3 --- @type integer
SPECIAL_WARP_TITLE            = -8 --- @type integer
SPECIAL_WARP_LEVEL_SELECT     = -9 --- @type integer

-----------------------
-- Renamed functions --
-----------------------

rom_hack_cam_set_collisions = camera_romhack_set_collisions
camera_romhack_allow_centering = camera_romhack_allow_switchable
camera_romhack_get_allow_centering = camera_romhack_get_allow_switchable
bhv_star_door_loop_2 = bhv_star_door_loop_update_render_state
absf_2 = math.abs
cur_obj_enable_rendering_2 = cur_obj_enable_rendering
cur_obj_can_mario_activate_textbox_2 = cur_obj_can_mario_activate_textbox
reset_rumble_timers_2 = reset_rumble_timers_vibrate
cur_obj_play_sound_1 = cur_obj_play_sound_if_visible
cur_obj_play_sound_2 = cur_obj_play_sound_and_rumble_if_visible
bit_shift_left = function (shift) return math.u8(1 << shift) end

--------------------
-- Math functions --
--------------------
--- Note: These functions were originally in smlua_math_utils.h,
--- but performed worse (~2x slower) than built-in Lua math functions

min = math.min
minf = math.min
max = math.max
maxf = math.max
sqr = math.sqr
sqrf = math.sqr
clamp = math.clamp
clampf = math.clamp
hypotf = math.hypot