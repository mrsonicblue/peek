import os
import gc
import pwmio
import time
import board
import digitalio
import displayio
import supervisor
import adafruit_touchscreen

# try:
#     if hasattr(board, "TFT_BACKLIGHT"):
#         backlight = pwmio.PWMOut(
#             board.TFT_BACKLIGHT
#         )  # pylint: disable=no-member
#     elif hasattr(board, "TFT_LITE"):
#         self._backlight = pwmio.PWMOut(
#             board.TFT_LITE
#         )  # pylint: disable=no-member
# except ValueError:
#     self._backlight = None
# self.set_backlight(1.0)  # turn on backlight

## os.stat("/pyportal_startup.bmp")
## for i in range(100, -1, -1):  # dim down
##     self.set_backlight(i / 100)
##     time.sleep(0.005)

button1 = digitalio.DigitalInOut(board.D3)
button1.direction = digitalio.Direction.INPUT
button1.pull = digitalio.Pull.UP

button2 = digitalio.DigitalInOut(board.D4)
button2.direction = digitalio.Direction.INPUT
button2.pull = digitalio.Pull.UP

touch = adafruit_touchscreen.Touchscreen(
    board.TOUCH_XL,
    board.TOUCH_XR,
    board.TOUCH_YD,
    board.TOUCH_YU,
    calibration=((5200, 59000), (5800, 57000)),
    size=(320, 240),
)

if False:
    display = board.DISPLAY

    root = displayio.Group(max_size=15)
    display.show(root)

    bg_file = open("pyportal_startup.bmp", "rb")
    background = displayio.OnDiskBitmap(bg_file)
    bg_sprite = displayio.TileGrid(
        background,
        pixel_shader=displayio.ColorConverter(),
        x=0,
        y=0,
    )
    root.append(bg_sprite)

# self.set_background("/pyportal_startup.bmp")
# try:
#     self.display.refresh(target_frames_per_second=60)
# except AttributeError:
#     self.display.wait_for_frame()
# for i in range(100):  # dim up
#     self.set_backlight(i / 100)
#     time.sleep(0.005)
# time.sleep(2)

button1_pressed = False
button2_pressed = False

while True:
    if button1_pressed:
        if button1.value:
            print("Button 1 released!")
            button1_pressed = False
    else:
        if not button1.value:
            print("Button 1 pressed!")
            button1_pressed = True

    if button2_pressed:
        if button2.value:
            print("Button 2 released!")
            button2_pressed = False
    else:
        if not button2.value:
            print("Button 2 pressed!")
            button2_pressed = True
    
    point = touch.touch_point
    if point:
        print("HI " + str(point))

    if supervisor.runtime.serial_bytes_available:
        line = input()
        print("ECHO: " + line)

    #print("Tick")
    time.sleep(0.05)

# # Set up where we'll be fetching data from
# DATA_SOURCE = "https://www.adafruit.com/api/quotes.php"
# QUOTE_LOCATION = [0, 'text']
# AUTHOR_LOCATION = [0, 'author']

# # the current working directory (where this file is)
# cwd = ("/"+__file__).rsplit('/', 1)[0]
# pyportal = PyPortal(url=DATA_SOURCE,
#                     json_path=(QUOTE_LOCATION, AUTHOR_LOCATION),
#                     status_neopixel=board.NEOPIXEL,
#                     default_bg=cwd+"/quote_background.bmp",
#                     text_font=cwd+"/fonts/Arial-ItalicMT-23.bdf",
#                     text_position=((20, 160),  # quote location
#                                    (5, 280)), # author location
#                     text_color=(0xFFFFFF,  # quote text color
#                                 0x8080FF), # author text color
#                     text_wrap=(40, # characters to wrap for quote
#                                0), # no wrap for author
#                     text_maxlen=(180, 30), # max text size for quote & author
#                    )

# # speed up projects with lots of text by preloading the font!
# pyportal.preload_font()

# while True:
#     try:
#         value = pyportal.fetch()
#         print("Response is", value)
#     except (ValueError, RuntimeError) as e:
#         print("Some error occured, retrying! -", e)
#     time.sleep(60)