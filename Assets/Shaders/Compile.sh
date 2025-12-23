slangc -target spirv -profile spirv_1_4 -matrix-layout-column-major -fvk-use-entrypoint-name -lang glsl -o Bin/Basic.spv Basic.slang
slangc -target spirv -profile spirv_1_4 -matrix-layout-column-major -fvk-use-entrypoint-name -lang glsl -o Bin/GeometryPass.spv GeometryPass.slang
slangc -target spirv -profile spirv_1_4 -matrix-layout-column-major -fvk-use-entrypoint-name -lang glsl -o Bin/LightingPass.spv LightingPass.slang
slangc -target spirv -profile spirv_1_4 -matrix-layout-column-major -fvk-use-entrypoint-name -lang glsl -o Bin/Cubemap.spv Cubemap.slang
slangc -target spirv -profile spirv_1_4 -matrix-layout-column-major -fvk-use-entrypoint-name -lang glsl -o Bin/ShadowDepthPass.spv ShadowDepthPass.slang
slangc -target spirv -profile spirv_1_4 -matrix-layout-column-major -fvk-use-entrypoint-name -lang glsl -o Bin/PBR.spv PBR.slang
slangc -target spirv -profile spirv_1_4 -matrix-layout-column-major -fvk-use-entrypoint-name -lang glsl -o Bin/EquirectangularToCubemap.spv EquirectangularToCubemap.slang
slangc -target spirv -profile spirv_1_4 -matrix-layout-column-major -fvk-use-entrypoint-name -lang glsl -o Bin/DiffuseIrradiance.spv DiffuseIrradiance.slang
