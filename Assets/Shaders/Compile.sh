slangc -target spirv -profile spirv_1_6 -matrix-layout-column-major -fvk-use-scalar-layout -fvk-use-entrypoint-name -lang glsl -g -o Bin/Basic.spv Basic.slang
slangc -target spirv -profile spirv_1_6 -matrix-layout-column-major -fvk-use-scalar-layout -fvk-use-entrypoint-name -lang glsl -g -o Bin/Cubemap.spv Cubemap.slang
slangc -target spirv -profile spirv_1_6 -matrix-layout-column-major -fvk-use-scalar-layout -fvk-use-entrypoint-name -lang glsl -g -o Bin/PBR.spv PBR.slang
slangc -target spirv -profile spirv_1_6 -matrix-layout-column-major -fvk-use-scalar-layout -fvk-use-entrypoint-name -lang glsl -g -o Bin/EquirectangularToCubemap.spv EquirectangularToCubemap.slang
slangc -target spirv -profile spirv_1_6 -matrix-layout-column-major -fvk-use-scalar-layout -fvk-use-entrypoint-name -lang glsl -g -o Bin/DiffuseIrradiance.spv DiffuseIrradiance.slang
slangc -target spirv -profile spirv_1_6 -matrix-layout-column-major -fvk-use-scalar-layout -fvk-use-entrypoint-name -lang glsl -g -o Bin/Prefilter.spv Prefilter.slang
slangc -target spirv -profile spirv_1_6 -matrix-layout-column-major -fvk-use-scalar-layout -fvk-use-entrypoint-name -lang glsl -g -o Bin/IntegrateBRDF.spv IntegrateBRDF.slang
