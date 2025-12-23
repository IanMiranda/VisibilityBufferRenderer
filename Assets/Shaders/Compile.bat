slangc -target spirv -profile spirv_1_4 -matrix-layout-column-major -fvk-use-entrypoint-name -lang glsl -o Bin/Basic.spv Basic.slang
slangc -target spirv -profile spirv_1_4 -matrix-layout-column-major -fvk-use-entrypoint-name -lang glsl -o Bin/Cubemap.spv Cubemap.slang
slangc -target spirv -profile spirv_1_4 -matrix-layout-column-major -fvk-use-entrypoint-name -lang glsl -o Bin/PBR.spv PBR.slang
slangc -target spirv -profile spirv_1_4 -matrix-layout-column-major -fvk-use-entrypoint-name -lang glsl -o Bin/EquirectangularToCubemap.spv EquirectangularToCubemap.slang
slangc -target spirv -profile spirv_1_4 -matrix-layout-column-major -fvk-use-entrypoint-name -lang glsl -o Bin/DiffuseIrradiance.spv DiffuseIrradiance.slang
slangc -target spirv -profile spirv_1_4 -matrix-layout-column-major -fvk-use-entrypoint-name -lang glsl -o Bin/Prefilter.spv Prefilter.slang
