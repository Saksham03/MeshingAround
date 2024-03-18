//HEAVILY based on the WavefrontReader class provided by
//Microsoft in their WavefrontConverter example, which is a part of their
//suite of Mesh Shader examples:
//https://github.com/microsoft/DirectX-Graphics-Samples/tree/master/Samples/Desktop/D3D12MeshShaders/src/WavefrontConverter
//barely anything is original here. This is mostly a stripped-down version of
//their class written again for my own understanding.


#pragma once

#include <fstream>
#include <unordered_map>
#include <DirectXCollision.h>

template<class index_t>
class ObjLoader
{
public:
    struct Vertex
    {
        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT3 normal;
        DirectX::XMFLOAT2 textureCoordinate;
    };    

    ObjLoader() noexcept : hasNormals(false), hasTexcoords(false) {}

	HRESULT LoadObj(_In_z_ const wchar_t* filename)
	{
        static const size_t MAX_POLY = 64;
        using namespace DirectX;

		std::wifstream InFile(filename);
		if (!InFile)
			return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

		std::vector<XMFLOAT3>   positions;
		std::vector<XMFLOAT3>   normals;
		std::vector<XMFLOAT2>   texCoords;
        VertexCache  vertexCache;
        uint32_t curSubset = 0;
		wchar_t strMaterialFilename[MAX_PATH] = {};

		for (;;)
		{
			std::wstring strCommand;
			InFile >> strCommand;
			if (!InFile)
				break;

			if (0 == wcscmp(strCommand.c_str(), L"v"))
			{
				// Vertex Position
				float x, y, z;
				InFile >> x >> y >> z;
				positions.push_back(XMFLOAT3(x, y, z));
			}
			else if (0 == wcscmp(strCommand.c_str(), L"vt"))
			{
				// Vertex TexCoord
				float u, v;
				InFile >> u >> v;
				texCoords.push_back(XMFLOAT2(u, v));
				hasTexcoords = true;
			}
			else if (0 == wcscmp(strCommand.c_str(), L"vn"))
			{
				// Vertex Normal
				float x, y, z;
				InFile >> x >> y >> z;
				normals.push_back(XMFLOAT3(x, y, z));
				hasNormals = true;
			}
			else if (0 == wcscmp(strCommand.c_str(), L"f"))
			{
				// Face
				int iPosition, iTexCoord, iNormal;
				Vertex vertex;

				uint32_t faceIndex[MAX_POLY];
				size_t iFace = 0;
				for (;;)
				{
					memset(&vertex, 0, sizeof(vertex));
					InFile >> iPosition;
					uint32_t vertexIndex = 0;
					HRESULT hr = ParseObjFaceLineComponent(iPosition, positions, vertexIndex);
					if (FAILED(hr)) return hr;
					vertex.position = positions[vertexIndex];

					if ('/' == InFile.peek())
					{
						InFile.ignore();

						if ('/' != InFile.peek())
						{
							// Optional texture coordinate
							InFile >> iTexCoord;
							uint32_t coordIndex = 0;
							HRESULT hr = ParseObjFaceLineComponent(iTexCoord, texCoords, coordIndex);
							if (FAILED(hr)) return hr;
							vertex.textureCoordinate = texCoords[coordIndex];
						}

						if ('/' == InFile.peek())
						{
							InFile.ignore();

							// Optional vertex normal
							InFile >> iNormal;
							uint32_t normIndex = 0;
							HRESULT hr = ParseObjFaceLineComponent(iNormal, normals, normIndex);
							if (FAILED(hr)) return hr;
							vertex.normal = normals[normIndex];
						}
					}

					// If a duplicate vertex doesn't exist, add this vertex to the Vertices
					// list. Store the index in the Indices array. The Vertices and Indices
					// lists will eventually become the Vertex Buffer and Index Buffer for
					// the mesh.
					uint32_t index = AddVertex(vertexIndex, &vertex, vertexCache);
					if (index == uint32_t(-1))
						return E_OUTOFMEMORY;

					faceIndex[iFace] = index;
					++iFace;

					// Check for more face data or end of the face statement
					bool faceEnd = false;
					for (;;)
					{
						wchar_t p = InFile.peek();

						if ('\n' == p || !InFile)
						{
							faceEnd = true;
							break;
						}
						else if (isdigit(p) || p == '-' || p == '+')
							break;

						InFile.ignore();
					}

					if (faceEnd)
						break;
				}

				if (iFace < 3)
				{
					// Need at least 3 points to form a triangle
					return E_FAIL;
				}

				// Convert polygons to triangles
				uint32_t i0 = faceIndex[0];
				uint32_t i1 = faceIndex[1];

				for (size_t j = 2; j < iFace; ++j)
				{
					indices.emplace_back(static_cast<index_t>(i0));
					indices.emplace_back(static_cast<index_t>(faceIndex[j]));
					indices.emplace_back(static_cast<index_t>(faceIndex[j - 1]));
					attributes.emplace_back(curSubset);
				}
				assert(attributes.size() * 3 == indices.size());
			}
			else if (0 == wcscmp(strCommand.c_str(), L"mtllib"))
			{
				// Material library
				InFile >> strMaterialFilename;
			}
			else if (0 == wcscmp(strCommand.c_str(), L"usemtl"))
			{
				// Material
				wchar_t strName[MAX_PATH] = {};
				InFile >> strName;

				bool bFound = false;
				uint32_t count = 0;
				for (auto it = materials.cbegin(); it != materials.cend(); ++it, ++count)
				{
					if (0 == wcscmp(it->strName, strName))
					{
						bFound = true;
						curSubset = count;
						break;
					}
				}

				if (!bFound)
				{
					Material mat;
					curSubset = static_cast<uint32_t>(materials.size());
					wcscpy_s(mat.strName, MAX_PATH - 1, strName);
					materials.emplace_back(mat);
				}
			}
		}
        return S_OK;
	}

	struct Material
	{
		DirectX::XMFLOAT3 vAmbient;
		DirectX::XMFLOAT3 vDiffuse;
		DirectX::XMFLOAT3 vSpecular;
		DirectX::XMFLOAT3 vEmissive;
		uint32_t nShininess;
		float fAlpha;

		bool bSpecular;
		bool bEmissive;

		wchar_t strName[MAX_PATH];
		wchar_t strTexture[MAX_PATH];
		wchar_t strNormalTexture[MAX_PATH];
		wchar_t strSpecularTexture[MAX_PATH];
		wchar_t strEmissiveTexture[MAX_PATH];
		wchar_t strRMATexture[MAX_PATH];

		Material() noexcept :
			vAmbient(0.2f, 0.2f, 0.2f),
			vDiffuse(0.8f, 0.8f, 0.8f),
			vSpecular(1.0f, 1.0f, 1.0f),
			vEmissive(0.f, 0.f, 0.f),
			nShininess(0),
			fAlpha(1.f),
			bSpecular(false),
			bEmissive(false),
			strName{},
			strTexture{},
			strNormalTexture{},
			strSpecularTexture{},
			strEmissiveTexture{},
			strRMATexture{}
		{
		}
	};

	std::vector<Vertex>     vertices;
	std::vector<index_t>    indices;
	std::vector<uint32_t>   attributes;
	std::vector<Material>   materials;
	bool                    hasNormals;
	bool                    hasTexcoords;
	DirectX::BoundingBox    bounds;

private:
	//custom function I wrote because OBJ parser was doing the same thing again & again
	template<typename T>
	HRESULT ParseObjFaceLineComponent(int iComponent, std::vector<T> components, uint32_t& index)
	{
		if (!iComponent)
		{
			// 0 is not allowed for index
			return E_UNEXPECTED;
		}
		else if (iComponent < 0)
		{
			// Negative values are relative indices
			index = uint32_t(ptrdiff_t(components.size()) + iComponent);
		}
		else
		{
			// OBJ format uses 1-based arrays
			index = uint32_t(iComponent - 1);
		}

		if (index >= components.size())
			return E_FAIL;

        return S_OK;
	}

	//The vertex cache and AddVertex function were copied as-is from the WavefrontReader class
	using VertexCache = std::unordered_multimap<uint32_t, uint32_t>;

	uint32_t AddVertex(uint32_t hash, const Vertex* pVertex, VertexCache& cache)
	{
		auto f = cache.equal_range(hash);

		for (auto it = f.first; it != f.second; ++it)
		{
			auto& tv = vertices[it->second];

			if (0 == memcmp(pVertex, &tv, sizeof(Vertex)))
			{
				return it->second;
			}
		}

		auto index = static_cast<uint32_t>(vertices.size());
		vertices.emplace_back(*pVertex);

		VertexCache::value_type entry(hash, index);
		cache.insert(entry);
		return index;
	}
};
