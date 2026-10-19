from os.path import abspath, join

Import("env")

project_dir = env.subst("$PROJECT_DIR")
repository_root = abspath(join(project_dir, "..", "..", ".."))
scf_source_dir = join(repository_root, "src", "Store Carry Forward")
local_lora_source_dir = join(repository_root, "lib", "LoRa", "src")
local_lora_include_dir = local_lora_source_dir
framework_dir = env.PioPlatform().get_package_dir(
    "framework-arduinoespressif32"
)
spi_include_dir = join(framework_dir, "libraries", "SPI", "src")

env.BuildSources("$BUILD_DIR/scf_production", scf_source_dir)
env.BuildSources("$BUILD_DIR/lora_production", local_lora_source_dir)
env.Append(CPPPATH=[scf_source_dir, local_lora_include_dir, spi_include_dir])
