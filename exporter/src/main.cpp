#include "Types.hpp"
#include "SimpleMesh.hpp"
#include "AssimpLoader.hpp"
#include "MeshWriter.hpp"
#include <iostream>

void PrintUsage(const char* programName)
{
    std::cout << "╔════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║          Mesh Converter Tool v1.0.0                        ║" << std::endl;
    std::cout << "║          Copyright (c) 2025 Luis Santos (aka djoker)      ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage: " << programName << " <input> <output> [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "Optimization:" << std::endl;
    std::cout << "  -O<0-2>           Optimization level (default: 1)" << std::endl;
    std::cout << "                      0: No optimization" << std::endl;
    std::cout << "                      1: Basic (cache, vertex reuse)" << std::endl;
    std::cout << "                      2: Aggressive (merge, cleanup)" << std::endl;
    std::cout << "  --no-optimize     Disable all optimizations" << std::endl;
    std::cout << "  --merge-meshes    Merge submeshes with same material" << std::endl;
    std::cout << std::endl;
    std::cout << "Geometry Generation:" << std::endl;
    std::cout << "  --gen-uvs         Generate UV coordinates if missing" << std::endl;
    std::cout << "  --gen-normals     Generate normals if missing (flat)" << std::endl;
    std::cout << "  --gen-smooth      Generate smooth normals (default)" << std::endl;
    std::cout << "  --gen-flat        Generate flat normals" << std::endl;
    std::cout << "  --gen-tangents    Generate tangents/bitangents (default: ON)" << std::endl;
    std::cout << "  --no-tangents     Skip tangent generation" << std::endl;
    std::cout << std::endl;
    std::cout << "Other:" << std::endl;
    std::cout << "  -v, --verbose     Verbose output" << std::endl;
    std::cout << "  --version         Show version" << std::endl;
    std::cout << std::endl;
    std::cout << "Animation:" << std::endl;
    std::cout << "  --export-anim <file>  Export animations to separate file(s)" << std::endl;
    std::cout << std::endl;
  
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << programName << " model.fbx model.mesh" << std::endl;
    std::cout << "  " << programName << " model.obj model.mesh -O2 --gen-smooth" << std::endl;
    std::cout << "  " << programName << " scan.ply scan.mesh --gen-uvs --gen-normals" << std::endl;
    std::cout << "  " << programName << " terrain.obj terrain.mesh --gen-flat -v" << std::endl;
    std::cout << "  " << programName << " character.fbx character.mesh --export-anim character.anim" << std::endl;
    std::cout << "  " << programName << " model.gltf model.mesh --export-anim anims.anim -v" << std::endl;

}

void PrintVersion()
{
    std::cout << "Mesh Converter Tool v1.0.0" << std::endl;
    std::cout << "Copyright (c) 2025 Luis Santos (aka djoker)" << std::endl;
    std::cout << "Build: " << __DATE__ << " " << __TIME__ << std::endl;
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        PrintUsage(argv[0]);
        return 1;
    }
    
    // Check version flag
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
    
    ConversionOptions opts;
    opts.verbose = false;
    opts.optimizationLevel = 1;
    opts.generateNormals = false;
    opts.generateSmoothNormals = true;  // Default: smooth
    opts.generateUVs = false;
    opts.generateTangents = true;       // Default: ON
    opts.mergeMeshes = false;

    bool exportAnimations = false;
    std::string animOutputFile;
    
    // Parse options
    for (int i = 3; i < argc; i++)
    {
        std::string arg = argv[i];
        
        if (arg == "-v" || arg == "--verbose")
        {
            opts.verbose = true;
        }
        else if (arg == "--version")
        {
            PrintVersion();
            return 0;
        }
        else if (arg == "--no-optimize")
        {
            opts.optimize = false;
            opts.optimizationLevel = 0;
        }
        else if (arg.substr(0, 2) == "-O")
        {
            opts.optimizationLevel = std::stoi(arg.substr(2));
            opts.optimize = opts.optimizationLevel > 0;
        }
        else if (arg == "--gen-uvs")
        {
            opts.generateUVs = true;
        }
        else if (arg == "--gen-normals")
        {
            opts.generateNormals = true;
        }
        else if (arg == "--gen-smooth")
        {
            opts.generateNormals = true;
            opts.generateSmoothNormals = true;
        }
        else if (arg == "--gen-flat")
        {
            opts.generateNormals = true;
            opts.generateSmoothNormals = false;
        }
        else if (arg == "--gen-tangents")
        {
            opts.generateTangents = true;
        }
        else if (arg == "--no-tangents")
        {
            opts.generateTangents = false;
        }
        else if (arg == "--merge-meshes")
        {
            opts.mergeMeshes = true;
        }
        else if (arg == "--export-anim" && i + 1 < argc)
        {
            exportAnimations = true;
            animOutputFile = argv[++i];
        }
        else
        {
            std::cerr << "Unknown option: " << arg << std::endl;
            std::cerr << "Use --help for usage information" << std::endl;
            return 1;
        }
    }
    
    // Print config
    std::cout << "Configuration:" << std::endl;
    std::cout << "──────────────────────────────────────" << std::endl;
    std::cout << "  Input:           " << inputFile << std::endl;
    std::cout << "  Output:          " << outputFile << std::endl;
    std::cout << std::endl;
    std::cout << "  Optimize:        ";
    if (opts.optimize)
        std::cout << "yes (level " << opts.optimizationLevel << ")" << std::endl;
    else
        std::cout << "no" << std::endl;
    
    std::cout << "  Generate UVs:    " << (opts.generateUVs ? "yes" : "no") << std::endl;
    std::cout << "  Generate normals: ";
    if (opts.generateNormals)
        std::cout << "yes (" << (opts.generateSmoothNormals ? "smooth" : "flat") << ")" << std::endl;
    else
        std::cout << "no" << std::endl;
    
    std::cout << "  Generate tangents: " << (opts.generateTangents ? "yes" : "no") << std::endl;
    std::cout << "  Merge meshes:    " << (opts.mergeMeshes ? "yes" : "no") << std::endl;
    std::cout << "──────────────────────────────────────" << std::endl;
    std::cout << std::endl;
    
    // Load
    std::cout << "Loading..." << std::endl;
    SimpleMesh mesh;
    AssimpLoader loader;
    loader.SetVerbose(opts.verbose);
    
    if (!loader.Load(inputFile, &mesh, opts))
    {
        std::cerr << "✗ Failed to load!" << std::endl;
        return 1;
    }
    std::cout << "✓ Load successful" << std::endl;
    std::cout << std::endl;
    

    if (exportAnimations && loader.HasAnimations())
    {
        std::cout << std::endl;
        std::cout << "Exporting animations..." << std::endl;
        
        AnimWriter animWriter;
        if (!animWriter.SaveAll(animOutputFile, loader.GetAnimations()))
        {
            std::cerr << "✗ Failed to write animation file!" << std::endl;
            return 1;
        }
        
        std::cout << "✓ Animations saved: " << animOutputFile << std::endl;
    }
    else if (exportAnimations && !loader.HasAnimations())
    {
        std::cout << std::endl;
        std::cout << "⚠ No animations found in input file" << std::endl;
    }


    // Save
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
