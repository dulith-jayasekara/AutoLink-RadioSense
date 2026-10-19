from os.path import abspath, dirname, join

Import("env")

project_dir = env.subst("$PROJECT_DIR")
repository_root = abspath(join(project_dir, "..", "..", ".."))
scf_source_dir = join(repository_root, "src", "Store Carry Forward")
local_lora_include_dir = join(repository_root, "lib", "LoRa", "src")
framework_dir = env.PioPlatform().get_package_dir(
    "framework-arduinoespressif32"
)
spi_include_dir = join(framework_dir, "libraries", "SPI", "src")

# Compile the real Component 4 production sources from this repository.
env.BuildSources("$BUILD_DIR/scf_production", scf_source_dir)
env.Append(CPPPATH=[scf_source_dir, local_lora_include_dir, spi_include_dir])
