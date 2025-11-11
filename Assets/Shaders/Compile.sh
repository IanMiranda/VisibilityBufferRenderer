slangc -target spirv -profile spirv_1_4 -matrix-layout-column-major -fvk-use-entrypoint-name -lang glsl -o Basic.spv Basic.slang
slangc -target spirv -profile spirv_1_4 -matrix-layout-column-major -fvk-use-entrypoint-name -lang glsl -o GeometryPass.spv GeometryPass.slang
slangc -target spirv -profile spirv_1_4 -matrix-layout-column-major -fvk-use-entrypoint-name -lang glsl -o LightingPass.spv LightingPass.slang
slangc -target spirv -profile spirv_1_4 -matrix-layout-column-major -fvk-use-entrypoint-name -lang glsl -o Cubemap.spv Cubemap.slang
slangc -target spirv -profile spirv_1_4 -matrix-layout-column-major -fvk-use-entrypoint-name -lang glsl -o ShadowDepthPass.spv ShadowDepthPass.slang