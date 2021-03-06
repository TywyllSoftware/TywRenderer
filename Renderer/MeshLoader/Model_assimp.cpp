/*
*	Copyright 2015-2016 Tomas Mikalauskas. All rights reserved.
*	GitHub repository - https://github.com/TywyllSoftware/TywRenderer
*	This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/
#include <RendererPch\stdafx.h>


//MeshLoader Includes
#include "Model_local.h"
#include "Material.h"
#include "MeshLoader\ImageManager.h"


//Vulkan Includes
#include "Vulkan\VulkanTextureLoader.h"


//Assimp Includes
#include <External\assimp\include\assimp\Importer.hpp> 
#include <External\assimp\include\assimp\scene.h>     
#include <External\assimp\include\assimp\postprocess.h>
#include <External\assimp\include\assimp\cimport.h>
#include <External\assimp\include\assimp\DefaultLogger.hpp>
#include <External\assimp\include\assimp\LogStream.hpp>




//Fucntions declared
bool InitFromScene(const aiScene* pScene, const std::string& Filename, std::vector<modelSurface_t>& entries, RenderModelAssimp::MaterialMap& materialMap);
void InitMesh(const aiMesh* paiMesh, const aiScene* pScene, modelSurface_t& entry, RenderModelAssimp::MaterialMap& materialMap, const std::string& Filename);
void LoadMaterials(const aiScene* pScene, std::vector<modelSurface_t>& entries);

RenderModelAssimp::RenderModelAssimp()
{

}

RenderModelAssimp::~RenderModelAssimp()
{

}



void RenderModelAssimp::InitFromFile(std::string fileName, std::string filePath)
{
	std::string file = filePath + fileName;

	// Change this line to normal if you not want to analyse the import process
	//Assimp::Logger::LogSeverity severity = Assimp::Logger::NORMAL;
	Assimp::Logger::LogSeverity severity = Assimp::Logger::VERBOSE;

	// Create a logger instance for Console Output
	Assimp::DefaultLogger::create("", severity, aiDefaultLogStream_STDOUT);

	Assimp::Importer Importer;
	const aiScene* pScene;
	uint32_t flags = aiProcess_FlipWindingOrder | aiProcess_Triangulate | aiProcess_PreTransformVertices | aiProcess_CalcTangentSpace | aiProcess_GenSmoothNormals;

#if defined(__ANDROID__)
	// Meshes are stored inside the apk on Android (compressed)
	// So they need to be loaded via the asset manager

	AAsset* asset = AAssetManager_open(assetManager, file.c_str(), AASSET_MODE_STREAMING);
	assert(asset);
	size_t size = AAsset_getLength(asset);

	assert(size > 0);

	void *meshData = malloc(size);
	AAsset_read(asset, meshData, size);
	AAsset_close(asset);

	pScene = Importer.ReadFileFromMemory(meshData, size, flags);

	free(meshData);
#else
	pScene = Importer.ReadFile(file.c_str(), flags);
#endif

	if (pScene)
	{
		InitFromScene(pScene, file, m_Entries, m_material);
	}
	else
	{
		printf("Error parsing '%s': '%s'\n", file.c_str(), Importer.GetErrorString());
#if defined(__ANDROID__)
		LOGE("Error parsing '%s': '%s'", file.c_str(), Importer.GetErrorString());
#endif
	}
}


void RenderModelAssimp::LoadModel()
{

}


srfTriangles_t *	RenderModelAssimp::AllocSurfaceTriangles(int numVerts, int numIndexes) const
{
	return nullptr;
}


void RenderModelAssimp::FreeSurfaceTriangles(srfTriangles_t *tris)
{

}


RenderModel * RenderModelAssimp::InstantiateDynamicModel()
{
	return nullptr;
}

const char	* RenderModelAssimp::getName() const
{
	return nullptr;
}


int RenderModelAssimp::getSize() const
{
	return 0;
}


void RenderModelAssimp::Clear(VkDevice device)
{
	for (auto& mesh : m_Entries)
	{
		SAFE_DELETE_ARRAY(mesh.geometry->indexes);
		SAFE_DELETE_ARRAY(mesh.geometry->verts);
		
		if (mesh.material != nullptr)
		{
			if (mesh.numMaterials > 0)
			{
				mesh.material->Clear(device);
				SAFE_DELETE_ARRAY(mesh.material);
			}
			else
			{
				//No textures were loaded but material was created so we have to destroy it.
//				mesh.material->Clear(device);
				SAFE_DELETE_ARRAY(mesh.material);
			}
		}
		SAFE_DELETE(mesh.geometry);
	}

	for (auto& material : m_material)
	{
		material.second->Clear(device);
		SAFE_DELETE_ARRAY(material.second);
	}
}


bool InitFromScene(const aiScene* pScene, const std::string& Filename, std::vector<modelSurface_t>& entries, RenderModelAssimp::MaterialMap& materialMap)
{
	uint32_t numVertices = 0;
	entries.resize(pScene->mNumMeshes);

	//Materilas
	LoadMaterials(pScene, entries);

	//Load geometry
	uint32_t index = 0;
	for (auto& modelSurface: entries)
	{
		const aiMesh* paiMesh = pScene->mMeshes[index];
		modelSurface.geometry = TYW_NEW srfTriangles_t;

		modelSurface.geometry->numVerts = paiMesh->mNumVertices;
		modelSurface.geometry->verts = TYW_NEW drawVert[paiMesh->mNumVertices];

		//Total vertices
		numVertices += paiMesh->mNumVertices;

		//Initialize the meshes
		InitMesh(paiMesh, pScene, modelSurface, materialMap, Filename);

		//Increment
		index++;
	}
	return true;
}


void LoadMaterials(const aiScene* pScene, std::vector<modelSurface_t>& entries)
{
	for (uint32_t i = 0; i < pScene->mNumMaterials; i++)
	{
		const aiMaterial* material = pScene->mMaterials[i];

		std::vector<std::tuple<std::string, VkTools::VulkanTexture*>> vkTextures;
		uint32_t NumbTextures = 0;

		uint32_t texIndex = 0;
		aiString path;

		if (material->GetTexture(aiTextureType_DIFFUSE, texIndex, &path) == AI_SUCCESS)
		{
			vkTextures.push_back(std::make_tuple(path.C_Str(), globalImage->GetImage(path.C_Str(), "../../../Assets/Textures/", VkFormat::VK_FORMAT_UNDEFINED)));
			NumbTextures++;
		}
		if (material->GetTexture(aiTextureType::aiTextureType_NORMALS, texIndex, &path) == AI_SUCCESS)
		{
			vkTextures.push_back(std::make_tuple(path.C_Str(), globalImage->GetImage(path.C_Str(), "../../../Assets/Textures/", VkFormat::VK_FORMAT_UNDEFINED)));
			NumbTextures++;
		}
		if (material->GetTexture(aiTextureType::aiTextureType_SPECULAR, texIndex, &path) == AI_SUCCESS)
		{
			vkTextures.push_back(std::make_tuple(path.C_Str(), globalImage->GetImage(path.C_Str(), "../../../Assets/Textures/", VkFormat::VK_FORMAT_UNDEFINED)));
			NumbTextures++;
		}


		Material* mat = TYW_NEW Material[NumbTextures];
		for (uint32_t j = 0; j < vkTextures.size(); j++)
		{
			mat[j].setTextureName(std::get<0>(vkTextures[j]));
			mat[j].setTexture(std::get<1>(vkTextures[j]), false);
		}

		//materialMap.insert(std::pair<std::string, Material*>(std::get<0>(vkTextures[0]), mat));
		entries[i].material = mat;
		entries[i].numMaterials = NumbTextures;
	}
}

void InitMesh(const aiMesh* paiMesh, const aiScene* pScene, modelSurface_t& entry, RenderModelAssimp::MaterialMap& materialMap, const std::string& Filename)
{
	aiColor3D pColor(0.f, 0.f, 0.f);
	pScene->mMaterials[paiMesh->mMaterialIndex]->Get(AI_MATKEY_COLOR_DIFFUSE, pColor);

	aiVector3D Zero3D(0.0f, 0.0f, 0.0f);
	for (uint32_t i = 0; i < paiMesh->mNumVertices; i++) 
	{
		aiVector3D* pPos = &(paiMesh->mVertices[i]);
		aiVector3D* pNormal = &(paiMesh->mNormals[i]);
		aiVector3D *pTexCoord;
		if (paiMesh->HasTextureCoords(0))
		{
			pTexCoord = &(paiMesh->mTextureCoords[0][i]);
		}
		else 
		{
			pTexCoord = &Zero3D;
		}

		aiVector3D* pTangent = (paiMesh->HasTangentsAndBitangents()) ? &(paiMesh->mTangents[i]) : &Zero3D;
		aiVector3D* pBiTangent = (paiMesh->HasTangentsAndBitangents()) ? &(paiMesh->mBitangents[i]) : &Zero3D;

		drawVert v(glm::vec3(pPos->x, -pPos->y, pPos->z),
				   glm::vec3(pNormal->x, pNormal->y, pNormal->z),
				   glm::vec3(pTangent->x, pTangent->y, pTangent->z),
				   glm::vec3(pBiTangent->x, pBiTangent->y, pBiTangent->z),
				   glm::vec3(pColor.r, pColor.g, pColor.b),
				   glm::vec2(pTexCoord->x, pTexCoord->y));


		//dim.max.x = fmax(pPos->x, dim.max.x);
		//dim.max.y = fmax(pPos->y, dim.max.y);
		//dim.max.z = fmax(pPos->z, dim.max.z);

		//dim.min.x = fmin(pPos->x, dim.min.x);
		//dim.min.y = fmin(pPos->y, dim.min.y);
		//dim.min.z = fmin(pPos->z, dim.min.z);

		entry.geometry->verts[i] = v;
	}
	//dim.size = dim.max - dim.min;

	entry.geometry->indexes = TYW_NEW uint32_t[paiMesh->mNumFaces*3];
	entry.geometry->numIndexes = paiMesh->mNumFaces*3;


	std::vector<uint32_t> test;
	test.resize(paiMesh->mNumFaces * 3);
	for (uint32_t i = 0; i < paiMesh->mNumFaces; i++)
	{
		const aiFace& Face = paiMesh->mFaces[i];
		if (Face.mNumIndices != 3)
			continue;

		entry.geometry->indexes[(i*3)]   = Face.mIndices[0];
		entry.geometry->indexes[(i * 3 + 1)] = Face.mIndices[1];
		entry.geometry->indexes[(i * 3 + 2)] = Face.mIndices[2];

		test[(i * 3)] = Face.mIndices[0];
		test[(i * 3 + 1)] = Face.mIndices[1];
		test[(i * 3 + 2)] = Face.mIndices[2];
	}
}