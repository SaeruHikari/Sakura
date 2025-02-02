/*****************************************************************************************
							 Copyrights   SaeruHikari
CreateDate:							2020.11.1
Description:	   Import Geometry from Assimp support files.
Details:
*******************************************************************************************/
#pragma once
#include <stdint.h>
#include <memory>
#include "Modules/Assimp/AssimpDefinations.hpp"
#include "tinyply.h"
#include "d3dUtil.h"
#include <chrono>

//Link necessary d3d12 libraries
#if defined(_X86_)
#if defined(DEBUG) || defined(_DEBUG)
#pragma comment(lib, "x86/Debug/tinyplyd.lib")
#else
#pragma comment(lib, "x86/Release/tinyply.lib")
#endif
#elif defined(_AMD64_)
#pragma comment(lib, "x64/Debug/tinyplyd64.lib")
#else
#pragma comment(lib, "x64/Release/tinyply64.lib")
#endif

struct StandardVertex;

namespace SScene
{
	class SakuraSceneNode;
}

using namespace std;
namespace HikaD3DUtils
{
	enum class ESupportFileForm : uint16_t
	{
		TEXT = 0,
		ASSIMP_SUPPORTFILE = 1,
		// now please use assimp to import ply file.
		PLY = 2,
		COUNT
	};

	class MeshImporter
	{
	public:
		///<summary>
		/// Import Mesh from a text data file with the special form in d3d12 book.
		/// Returns an unique pointer of Mesh Geometry
		/// with only one submesh.
		///</summary>
		static std::unique_ptr<Dx12MeshGeometry> ImportMesh(ID3D12Device* device, 
			ID3D12GraphicsCommandList* CommandList, std::string FilePath, ESupportFileForm FileForm = ESupportFileForm::TEXT);
	private:
		static void processNode(aiNode* node, const aiScene* scene, std::vector<StandardVertex>& vertices, std::vector<std::uint32_t>& indices);
		static void processMesh(aiMesh* mesh, const aiScene* scene, std::vector<StandardVertex>& vertices, std::vector<std::uint32_t>& indices);
		static void read_ply_file(const std::string& filepath, std::vector<StandardVertex>& vertices, std::vector<std::uint32_t>& indices);

	private:
		class manual_timer
		{
			std::chrono::high_resolution_clock::time_point t0;
			double timestamp{ 0.f };
		public:
			void start() { t0 = std::chrono::high_resolution_clock::now(); }
			void stop() { timestamp = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - t0).count() * 1000; }
			const double& get() { return timestamp; }
		};

		struct float2 { float x, y; };
		struct float3 { float x, y, z; };
		struct double3 { double x, y, z; };
		struct uint3 { uint32_t x, y, z; };
		struct uint4 { uint32_t x, y, z, w; };
	};
}

