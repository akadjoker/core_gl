#include "Types.hpp"
#include "SimpleMesh.hpp"
#include "MeshWriter.hpp"
#include "OgreMeshLoader.hpp"
#include <iostream>

void PrintUsage(const char* programName)
{
    std::cout << "╔════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║          mesh2h3d — native Ogre .mesh -> .h3d converter    ║" << std::endl;
    std::cout << "║          Copyright (c) 2026 Luis Santos (aka djoker)       ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage: " << programName << " <input.mesh> <output.h3d> [-v]" << std::endl;
    std::cout << std::endl;
    std::cout << "Reads Ogre's binary .mesh format directly (MeshSerializer_v1.100," << std::endl;
    std::cout << "Ogre >= 1.10) — no Assimp. Static meshes only: no skeleton, bone" << std::endl;
    std::cout << "weights or animation." << std::endl;
    std::cout << std::endl;
    std::cout << "  -v, --verbose     Verbose output" << std::endl;
    std::cout << "  --version         Show version" << std::endl;
}

void PrintVersion()
{
    std::cout << "mesh2h3d v1.0.0" << std::endl;
    std::cout << "Copyright (c) 2026 Luis Santos (aka djoker)" << std::endl;
    std::cout << "Build: " << __DATE__ << " " << __TIME__ << std::endl;
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        PrintUsage(argv[0]);
        return 1;
    }

    if (std::string(argv[1]) == "--version")
    {
        PrintVersion();
        return 0;
    }

    if (argc < 3)
    {
        PrintUsage(argv[0]);
        return 1;
    }

    std::string inputFile = argv[1];
    std::string outputFile = argv[2];
    bool verbose = false;

    for (int i = 3; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "-v" || arg == "--verbose")
            verbose = true;
        else
        {
            std::cerr << "Unknown option: " << arg << std::endl;
            return 1;
        }
    }

    std::cout << "Input:  " << inputFile << std::endl;
    std::cout << "Output: " << outputFile << std::endl;
    std::cout << std::endl;

    SimpleMesh mesh;
    OgreMeshLoader loader;
    loader.SetVerbose(verbose);

    std::cout << "Loading..." << std::endl;
    if (!loader.Load(inputFile, &mesh))
    {
        std::cerr << "✗ Failed to load!" << std::endl;
        return 1;
    }
    std::cout << "✓ Load successful — " << mesh.GetMaterialCount() << " material(s), "
              << mesh.GetBufferCount() << " submesh(es)" << std::endl;
    std::cout << std::endl;

    std::cout << "Writing mesh..." << std::endl;
    MeshWriter writer;
    if (!writer.Save(&mesh, outputFile))
    {
        std::cerr << "✗ Failed to save!" << std::endl;
        return 1;
    }
    std::cout << "✓ Mesh saved: " << outputFile << std::endl;
    std::cout << std::endl;

    std::cout << "════════════════════════════════════════" << std::endl;
    std::cout << "✓ Conversion completed successfully!" << std::endl;
    std::cout << "════════════════════════════════════════" << std::endl;

    return 0;
}
