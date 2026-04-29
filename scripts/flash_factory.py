"""Registers a 'flash-factory' custom pio target.

Erases the entire flash and writes firmware.factory.bin at offset 0x0.
Use this to bring a device up clean — no NVS, no saved wifi creds, so
it boots into AP provisioning mode. Also the right path for first-time
flashing of a fresh device.

  pio run -e tdongle-s3 -t flash-factory

Uses pio's bundled esptool (tool-esptoolpy) and the env's UPLOAD_PORT /
UPLOAD_SPEED settings, so behavior matches `pio run -t upload` for port
detection and baud.
"""
import os

Import("env")

factory_bin = os.path.join("$BUILD_DIR", "firmware.factory.bin")
chip = env.BoardConfig().get("build.mcu", "esp32")
uploader = env.subst("$PYTHONEXE $UPLOADER")

env.AddCustomTarget(
    name="flash-factory",
    dependencies="buildprog",
    actions=[
        f'{uploader} --chip {chip} --port "$UPLOAD_PORT" --baud $UPLOAD_SPEED erase_flash',
        f'{uploader} --chip {chip} --port "$UPLOAD_PORT" --baud $UPLOAD_SPEED write_flash 0x0 "{factory_bin}"',
    ],
    title="Flash Factory",
    description="Erase flash + write firmware.factory.bin (clean prov-mode boot)",
)
