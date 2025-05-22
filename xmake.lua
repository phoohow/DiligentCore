target("DiligentCore")
    set_kind("shared")

    -- Add all includes from core
    add_includedirs("Primitives/interface")
    add_includedirs("Platforms/interface", "Platforms/Basic/include", "Platforms/Basic/interface", "Platforms/Win32/interface")
    add_includedirs("Common/include", "Common/interface")
    add_includedirs("Graphics/GraphicsAccessories/interface")
    add_includedirs("Graphics/GraphicsEngine/include", "Graphics/GraphicsEngine/interface")
    add_includedirs("Graphics/ShaderTools/include")
    add_includedirs("Graphics/HLSL2GLSLConverterLib/interface", "Graphics/HLSL2GLSLConverterLib/include")
    add_includedirs("Graphics/GraphicsEngineNextGenBase/include")
    add_includedirs("Graphics/GraphicsEngineD3DBase/include", "Graphics/GraphicsEngineD3DBase/interface")
    add_includedirs("Graphics/GraphicsEngineD3D12/include", "Graphics/GraphicsEngineD3D12/interface")

    -- Add all files from core
    add_files("Primitives/src/*.cpp")
    add_files("Platforms/Basic/src/*.cpp", "Platforms/Win32/src/*.cpp")
    add_files("Common/src/*.cpp")
    add_files("Graphics/GraphicsAccessories/src/*.cpp")
    add_files("Graphics/GraphicsEngine/src/*.cpp")
    add_files("Graphics/ShaderTools/src/*.cpp|*UWP.cpp|*Linux.cpp|WGSL*.cpp|DXILUtilsStub.cpp")
    add_files("Graphics/HLSL2GLSLConverterLib/src/*.cpp")
    add_files("Graphics/GraphicsEngineNextGenBase/src/*.cpp")
    add_files("Graphics/GraphicsEngineD3DBase/src/*.cpp")
    add_files("Graphics/GraphicsEngineD3D12/src/*.cpp")

    if is_plat("windows") then
        add_defines("PLATFORM_WIN32")
        add_defines("D3D12_SUPPORTED")
        add_defines("USE_D3D12_LOADER=1")
        add_defines("ENABLE_DLL")
        add_links("Comdlg32")-- for open file dialogs
        add_links("D3D12", "d3dcompiler", "dxgi")
    end

    -- add 3rd party includes
    add_includedirs("ThirdParty/glslang")
    add_includedirs("ThirdParty/SPIRV-Tools/include")
    add_includedirs("ThirdParty/SPIRV-Tools")
    add_includedirs("ThirdParty/SPIRV-Headers/include")
    add_includedirs("ThirdParty/SPIRV-Cross")
    add_includedirs("ThirdParty/DirectXShaderCompiler")

    -- add 3rd party files
    add_files("ThirdParty/glslang/SPIRV/*.cpp")
    add_defines("ENABLE_HLSL")
    add_files("ThirdParty/glslang/glslang/MachineIndependent/*.cpp")
    add_files("ThirdParty/glslang/glslang/MachineIndependent/preprocessor/*.cpp")
    add_files("ThirdParty/glslang/glslang/GenericCodeGen/*.cpp")
    add_files("ThirdParty/glslang/glslang/HLSL/*.cpp")
    add_files("ThirdParty/SPIRV-Cross/*.cpp")
    add_files("ThirdParty/SPIRV-Tools/Source/*.cpp")
    add_files("ThirdParty/SPIRV-Tools/Source/opt/*.cpp")
    add_files("ThirdParty/SPIRV-Tools/Source/val/*.cpp")
    add_files("ThirdParty/SPIRV-Tools/Source/util/*.cpp")
    add_files("ThirdParty/GPUOpenShaderUtils/*.cpp")

    add_headerfiles("*/interface/*.h", {public=true})
target_end()
