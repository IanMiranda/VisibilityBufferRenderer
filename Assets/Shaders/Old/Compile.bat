glslangValidator -V -H -e VSMain --source-entrypoint VSMain -o Basic.vert.spv Basic.vert
glslangValidator -V -H -e FSMain --source-entrypoint FSMain -o Basic.frag.spv Basic.frag
spirv-link Basic.vert.spv Basic.frag.spv -o Basic.spv

# slangc -target spirv -profile spirv_1_4 -matrix-layout-column-major -fvk-use-entrypoint-name -entry VSMain -o Basic.vert.spv Basic.slan
# slangc -target spirv -profile spirv_1_4 -matrix-layout-column-major -fvk-use-entrypoint-name -entry FSMain -o Basic.frag.spv Basic.slang
# spirv-link Basic.vert.spv Basic.frag.spv -o Basic.spv

dxc Cubemap.hlsl -Fo Cubemap.vert.spv -spirv -E VSMain -T vs_6_0
dxc Cubemap.hlsl -Fo Cubemap.frag.spv -spirv -E FSMain -T ps_6_0
spirv-link Cubemap.vert.spv Cubemap.frag.spv -o Cubemap.spv

dxc ShadowDepthPass.hlsl -Fo ShadowDepthPass.spv -spirv -E VSMain -T vs_6_0