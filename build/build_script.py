Import("env")
import os

OUTPUT_DIR = f"build{os.path.sep}firmware.bin"


def merge_bins(source, target, env):
  # 获取除了APP_BIN之外的其他需要烧录的bin文件列表
  flash_images = env.Flatten(env.get("FLASH_EXTRA_IMAGES", []))
  # 添加主程序bin文件及其偏移地址
  flash_images += [
    env.get("ESP32_APP_OFFSET"),
    f"$BUILD_DIR{os.path.sep}${{PROGNAME}}.bin",
  ]

  # 执行合并bin文件命令
  cmd = " ".join(
    [
      "$PYTHONEXE",
      "$OBJCOPY",
      "--chip",
      env.get("BOARD_MCU"),
      "merge-bin",
      "--output",
      f"$PROJECT_DIR{os.path.sep}{OUTPUT_DIR}",
    ]
    + flash_images
  )
  env.Execute(cmd)


env.AddPostAction(f"$BUILD_DIR{os.path.sep}${{PROGNAME}}.bin", [merge_bins])
