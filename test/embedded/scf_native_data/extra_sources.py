from os.path import abspath, join

Import("env")

root = abspath(join(env.subst("$PROJECT_DIR"), "..", "..", ".."))
scf = join(root, "src", "Store Carry Forward")
lora = join(root, "lib", "LoRa", "src")
framework = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
env.Append(CPPPATH=[scf, lora, join(framework, "libraries", "SPI", "src")])
env.BuildSources("$BUILD_DIR/scf_production", scf,
                 src_filter="+<SCF_Message.cpp> +<SCF_Codec.cpp> +<SCF_Buffer.cpp>")
env.BuildSources("$BUILD_DIR/lora_production", lora)
