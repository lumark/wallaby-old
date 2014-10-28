#pragma once

#include <sys/time.h>
#include <kangaroo/platform.h>
#include "BoundedVolumeGrid.h"
#include <kangaroo/MarchingCubesTables.h>
#include <assimp/cexport.h>
#include <assimp/scene.h>

namespace roo {

KANGAROO_EXPORT
inline double _Tic()
{
  struct timeval tv;
  gettimeofday(&tv, 0);
  return tv.tv_sec  + 1e-6 * (tv.tv_usec);
}

////////////////////////////////////////////////////////////////////////////////
KANGAROO_EXPORT
inline double _Toc( double dSec )
{
  return _Tic() - dSec;
}


// =============================================================================
KANGAROO_EXPORT
inline aiMesh* MeshFromLists(
    const std::vector<aiVector3D>&                    verts,
    const std::vector<aiVector3D>&                    norms,
    const std::vector<aiFace>&                        faces,
    const std::vector<aiColor4D>&                     colors
    ) {
  aiMesh* mesh = new aiMesh();
  mesh->mPrimitiveTypes = aiPrimitiveType_TRIANGLE;

  mesh->mNumVertices = verts.size();
  mesh->mVertices = new aiVector3D[verts.size()];
  for(unsigned int i=0; i < verts.size(); ++i) {
    mesh->mVertices[i] = verts[i];
  }

  if(norms.size() == verts.size()) {
    mesh->mNormals = new aiVector3D[norms.size()];
    for(unsigned int i=0; i < norms.size(); ++i) {
      mesh->mNormals[i] = norms[i];
    }
  }else{
    mesh->mNormals = 0;
  }

  mesh->mNumFaces = faces.size();
  mesh->mFaces = new aiFace[faces.size()];
  for(unsigned int i=0; i < faces.size(); ++i) {
    mesh->mFaces[i] = faces[i];
  }

  if( colors.size() == verts.size()) {
    mesh->mColors[0] = new aiColor4D[colors.size()];
    for(unsigned int i=0; i < colors.size(); ++i) {
      mesh->mColors[0][i] = colors[i];
    }
  }

  return mesh;
}

// =============================================================================
KANGAROO_EXPORT
inline void SaveMeshGridToFile(
    std::string         sFilename,
    aiMesh*             pMesh,
    std::string         sFormat ="ply")
{
  // Create root node which indexes first mesh
  aiNode* root = new aiNode();
  root->mNumMeshes = 1;
  root->mMeshes = new unsigned int[root->mNumMeshes];
  root->mMeshes[0] = 0;
  root->mName = "root";

  aiMaterial* material = new aiMaterial();

  // Create scene to contain root node and mesh
  aiScene scene;
  scene.mRootNode = root;
  scene.mNumMeshes = 1;
  scene.mMeshes = new aiMesh*[scene.mNumMeshes];
  scene.mMeshes[0] = pMesh;
  scene.mNumMaterials = 1;
  scene.mMaterials = new aiMaterial*[scene.mNumMaterials];
  scene.mMaterials[0] = material;

  aiReturn res = aiExportScene(&scene, sFormat.c_str(),
                               (sFilename + "."+sFormat.c_str()).c_str(), 0);
  if(res == 0)
  {
    std::cout << "Mesh export success: " << res << std::endl;
  }
  else
  {
    std::cerr << "Mesh export fail: " << res << std::endl;
  }
}


// =============================================================================
//fGetOffset finds the approximate point of intersection of the surface
// between two points with the values fValue1 and fValue2
KANGAROO_EXPORT
inline float fGetOffset(float fValue1, float fValue2, float fValueDesired)
{
  const double fDelta = fValue2 - fValue1;
  if(fDelta == 0.0) {
    return 0.5;
  }
  return (fValueDesired - fValue1)/fDelta;
}


// =============================================================================
//vMarchCube performs the Marching Cubes algorithm on a single cube
// USER SHOULD MAKE SURE VOXEL EXIST!
KANGAROO_EXPORT
template<typename T, typename TColor>
void vMarchCubeGrid(
    BoundedVolumeGrid<T,roo::TargetHost, Manage>&       vol,
    BoundedVolumeGrid<TColor,roo::TargetHost, Manage>&  volColor,
    int x, int y, int z,
    std::vector<aiVector3D>&                            verts,
    std::vector<aiVector3D>&                            norms,
    std::vector<aiFace>&                                faces,
    std::vector<aiColor4D>&                             colors,
    float fTargetValue                                  = 0.0f
    )
{
  const float3 p = vol.VoxelPositionInUnits(x,y,z);
  const float3 fScale = vol.VoxelSizeUnits();

  //Make a local copy of the values at the cube's corners
  float afCubeValue[8];
  for(int iVertex = 0; iVertex < 8; iVertex++)
  {
    if(vol.CheckIfVoxelExist(
         int(x+a2fVertexOffset[iVertex][0]),
         int(y+a2fVertexOffset[iVertex][1]),
         int(z+a2fVertexOffset[iVertex][2])) == true)
    {
      afCubeValue[iVertex] =
          vol(int(x+a2fVertexOffset[iVertex][0]),
          int(y+a2fVertexOffset[iVertex][1]),
          int(z+a2fVertexOffset[iVertex][2]));
    }
    else
    {
      return;
    }

    if(!std::isfinite(afCubeValue[iVertex])) return;
  }

  //Find which vertices are inside of the surface and which are outside
  int iFlagIndex = 0;
  for(int iVertexTest = 0; iVertexTest < 8; iVertexTest++)
  {
    if(afCubeValue[iVertexTest] <= fTargetValue)
      iFlagIndex |= 1<<iVertexTest;
  }

  //Find which edges are intersected by the surface
  int iEdgeFlags = aiCubeEdgeFlags[iFlagIndex];

  // If the cube is entirely inside or outside of the surface,
  // then there will be no intersections
  if(iEdgeFlags == 0) {
    return;
  }

  //Find the point of intersection of the surface with each edge
  //Then find the normal to the surface at those points
  float3 asEdgeVertex[12];
  float3 asEdgeNorm[12];

  for(int iEdge = 0; iEdge < 12; iEdge++)
  {
    //if there is an intersection on this edge
    if(iEdgeFlags & (1<<iEdge))
    {
      float fOffset = fGetOffset(afCubeValue[ a2iEdgeConnection[iEdge][0] ],
          afCubeValue[ a2iEdgeConnection[iEdge][1] ], fTargetValue);

      asEdgeVertex[iEdge] = make_float3(
            p.x + (a2fVertexOffset[ a2iEdgeConnection[iEdge][0] ][0]  +
          fOffset * a2fEdgeDirection[iEdge][0]) * fScale.x,
          p.y + (a2fVertexOffset[ a2iEdgeConnection[iEdge][0] ][1]  +
          fOffset * a2fEdgeDirection[iEdge][1]) * fScale.y,
          p.z + (a2fVertexOffset[ a2iEdgeConnection[iEdge][0] ][2]  +
          fOffset * a2fEdgeDirection[iEdge][2]) * fScale.z );

      const float3 deriv = vol.GetUnitsBackwardDiffDxDyDz( asEdgeVertex[iEdge] );
      asEdgeNorm[iEdge] = deriv / length(deriv);

      if( !std::isfinite(asEdgeNorm[iEdge].x) ||
          !std::isfinite(asEdgeNorm[iEdge].y) ||
          !std::isfinite(asEdgeNorm[iEdge].z) )
      {
        asEdgeNorm[iEdge] = make_float3(0,0,0);
      }
    }
  }


  // Draw the triangles that were found.  There can be up to five per cube
  for(int iTriangle = 0; iTriangle < 5; iTriangle++)
  {
    if(a2iTriangleConnectionTable[iFlagIndex][3*iTriangle] < 0)
      break;

    aiFace face;
    face.mNumIndices = 3;
    face.mIndices = new unsigned int[face.mNumIndices];

    for(int iCorner = 0; iCorner < 3; iCorner++)
    {
      int iVertex = a2iTriangleConnectionTable[iFlagIndex][3*iTriangle+iCorner];

      face.mIndices[iCorner] = verts.size();
      verts.push_back(aiVector3D(asEdgeVertex[iVertex].x,
                                 asEdgeVertex[iVertex].y,
                                 asEdgeVertex[iVertex].z) );

      norms.push_back(aiVector3D(asEdgeNorm[iVertex].x,
                                 asEdgeNorm[iVertex].y,
                                 asEdgeNorm[iVertex].z) );

      if(volColor.IsValid()) {
        const TColor c = volColor.GetUnitsTrilinearClamped(asEdgeVertex[iVertex]);
        float3 sColor = roo::ConvertPixel<float3,TColor>(c);
        colors.push_back(aiColor4D(sColor.x, sColor.y, sColor.z, 1.0f));
      }
    }

    faces.push_back(face);
  }
}


//////////////////////////////////////////
// Save SDF
//////////////////////////////////////////
// =============================================================================
KANGAROO_EXPORT
template<typename T, typename TColor>
void SaveMeshGrid(
    std::string                                       filename,
    const BoundedVolumeGrid<T,TargetHost,Manage>      vol,
    const BoundedVolumeGrid<TColor,TargetHost,Manage> volColor );

// =============================================================================
KANGAROO_EXPORT
template<typename T, typename Manage>
void SaveMeshGrid(
    std::string                                       filename,
    BoundedVolumeGrid<T,TargetDevice,Manage>&         vol )
{
  roo::BoundedVolumeGrid<T,roo::TargetHost,roo::Manage> hvol;
  hvol.init(vol.m_w, vol.m_h, vol.m_d, vol.m_nVolumeGridRes, vol.m_bbox);
  hvol.CopyAndInitFrom(vol);

  roo::BoundedVolumeGrid<float,roo::TargetHost,roo::Manage> hvolcolor;
  hvolcolor.init(1,1,1, vol.m_nVolumeGridRes,vol.m_bbox );

  SaveMeshGrid<T,float>(filename, hvol, hvolcolor);
}

// =============================================================================
KANGAROO_EXPORT
template<typename T, typename TColor, typename Manage>
void SaveMeshGrid(
    std::string                                       filename,
    BoundedVolumeGrid<T,TargetDevice,Manage>&         vol,
    BoundedVolumeGrid<TColor,TargetDevice,Manage>&    volColor )
{
  roo::BoundedVolumeGrid<T,roo::TargetHost,roo::Manage> hvol;
  hvol.init(vol.m_w, vol.m_h, vol.m_d, vol.m_nVolumeGridRes,vol.m_bbox);
  hvol.CopyAndInitFrom(vol);

  roo::BoundedVolumeGrid<TColor,roo::TargetHost,roo::Manage> hvolcolor;
  hvolcolor.init(volColor.m_w, volColor.m_h, volColor.m_d,
                 volColor.m_nVolumeGridRes,volColor.m_bbox);

  hvolcolor.CopyAndInitFrom(volColor);

  // save
  SaveMeshGrid<T,TColor, Manage>(filename, hvol, hvolcolor);
}


// =============================================================================
// now do it for each grid instead of each voxel
KANGAROO_EXPORT
template<typename T, typename TColor, typename Manage>
aiMesh* GetMeshGrid(
    BoundedVolumeGrid<T, TargetHost, Manage>            vol,
    BoundedVolumeGrid<TColor, TargetHost, Manage>       volColor )
{
  std::vector<aiVector3D>   verts;
  std::vector<aiVector3D>   norms;
  std::vector<aiFace>       faces;
  std::vector<aiColor4D>    colors;

  // scan each grid..
  int nNumSkip =0;
  int nNumSave =0;

  for(int i=0;i!=vol.m_nGridRes_w;i++)
  {
    for(int j=0;j!=vol.m_nGridRes_h;j++)
    {
      for(int k=0;k!=vol.m_nGridRes_d;k++)
      {
        if(vol.CheckIfBasicSDFActive(vol.GetIndex(i,j,k)) == true)
        {
          GenMeshSingleGrid(vol,volColor,i,j,k,verts, norms, faces, colors);
          nNumSave++;
        }
        else
        {
          nNumSkip++;
        }
      }
    }
  }

  return MeshFromLists(verts,norms,faces,colors);
}

// =============================================================================
KANGAROO_EXPORT
template<typename T, typename TColor, typename Manage>
aiMesh* GetMeshGrid(
    BoundedVolumeGrid<T,TargetDevice,Manage>&         vol,
    BoundedVolumeGrid<TColor,TargetDevice,Manage>&    volColor )
{
  roo::BoundedVolumeGrid<T,roo::TargetHost,roo::Manage> hvol;
  hvol.init(vol.m_w, vol.m_h, vol.m_d, vol.m_nVolumeGridRes,vol.m_bbox);
  hvol.CopyAndInitFrom(vol);

  roo::BoundedVolumeGrid<TColor,roo::TargetHost,roo::Manage> hvolcolor;
  hvolcolor.init(volColor.m_w, volColor.m_h, volColor.m_d,
                 volColor.m_nVolumeGridRes,volColor.m_bbox);

  hvolcolor.CopyAndInitFrom(volColor);

  // save
  return GetMeshGrid<T,TColor, Manage>(hvol, hvolcolor);
}


// =============================================================================
// now do it for each grid instead of each voxel
KANGAROO_EXPORT
template<typename T, typename TColor>
void GenMeshSingleGrid(
    BoundedVolumeGrid<T, TargetHost, Manage>&           vol,
    BoundedVolumeGrid<TColor, TargetHost, Manage>&      volColor,
    int i,int j,int k,
    std::vector<aiVector3D>&                            verts,
    std::vector<aiVector3D>&                            norms,
    std::vector<aiFace>&                                faces,
    std::vector<aiColor4D>&                             colors)
{
  // for each voxel in the grid
  for(GLint x=0;x!=vol.m_nVolumeGridRes;x++)
  {
    for(GLint y=0;y!=vol.m_nVolumeGridRes;y++)
    {
      for(GLint z=0;z!=vol.m_nVolumeGridRes;z++)
      {
        if(vol.CheckIfVoxelExist(i*vol.m_nVolumeGridRes + x,
                                 j*vol.m_nVolumeGridRes + y,
                                 k*vol.m_nVolumeGridRes + z) == true)
        {
          // get voxel index for each grid.
          roo::vMarchCubeGrid(vol, volColor,
                              i*vol.m_nVolumeGridRes + x,
                              j*vol.m_nVolumeGridRes + y,
                              k*vol.m_nVolumeGridRes + z,
                              verts, norms, faces, colors);
        }
      }
    }
  }
}


// =============================================================================
// now do it for each grid instead of each voxel
KANGAROO_EXPORT
template<typename T, typename TColor, typename Manage>
void SaveMeshGrid(
    std::string                                         filename,
    BoundedVolumeGrid<T, TargetHost, Manage>            vol,
    BoundedVolumeGrid<TColor, TargetHost, Manage>       volColor )
{
  double dTime = _Tic();

  std::vector<aiVector3D>   verts;
  std::vector<aiVector3D>   norms;
  std::vector<aiFace>       faces;
  std::vector<aiColor4D>    colors;

  // scan each grid..
  int nNumSkip =0;
  int nNumSave =0;

  for(int i=0;i!=vol.m_nGridRes_w;i++)
  {
    for(int j=0;j!=vol.m_nGridRes_h;j++)
    {
      for(int k=0;k!=vol.m_nGridRes_d;k++)
      {
        if(vol.CheckIfBasicSDFActive(vol.GetIndex(i,j,k)) == true)
        {
          GenMeshSingleGrid(vol,volColor,i,j,k,verts, norms, faces, colors);
          nNumSave++;
        }
        else
        {
          nNumSkip++;
        }
      }
    }
  }

  aiMesh* mesh = MeshFromLists(verts,norms,faces,colors);

  printf("Finish march cube grid sdf. Save %d grids. Skip %d grids. Use time %f; \n",
         nNumSave, nNumSkip, _Toc(dTime));

  SaveMeshGridToFile(filename, mesh, "obj");
}




// =============================================================================
KANGAROO_EXPORT
template<typename T, typename TColor>
void SaveMeshGridSepreate(
    std::string                                       filename,
    const BoundedVolumeGrid<T,TargetHost,Manage>      vol,
    const BoundedVolumeGrid<TColor,TargetHost,Manage> volColor );

// =============================================================================
KANGAROO_EXPORT
template<typename T, typename Manage>
void SaveMeshGridSepreate(
    std::string                                       filename,
    BoundedVolumeGrid<T,TargetDevice,Manage>&         vol )
{
  roo::BoundedVolumeGrid<T,roo::TargetHost,roo::Manage> hvol;
  hvol.init(vol.m_w, vol.m_h, vol.m_d, vol.m_nVolumeGridRes, vol.m_bbox);
  hvol.CopyAndInitFrom(vol);

  roo::BoundedVolumeGrid<float,roo::TargetHost,roo::Manage> hvolcolor;
  hvolcolor.init(1,1,1, vol.m_nVolumeGridRes,vol.m_bbox );

  SaveMeshGridSepreate<T,float>(filename, hvol, hvolcolor);
}

// =============================================================================
KANGAROO_EXPORT
template<typename T, typename TColor, typename Manage>
void SaveMeshGridSepreate(
    std::string                                      filename,
    BoundedVolumeGrid<T,TargetDevice,Manage>&        vol,
    BoundedVolumeGrid<TColor,TargetDevice,Manage>&   volColor )
{
  roo::BoundedVolumeGrid<T,roo::TargetHost,roo::Manage> hvol;
  hvol.init(vol.m_w, vol.m_h, vol.m_d, vol.m_nVolumeGridRes,vol.m_bbox);
  hvol.CopyAndInitFrom(vol);

  roo::BoundedVolumeGrid<TColor,roo::TargetHost,roo::Manage> hvolcolor;
  hvolcolor.init(volColor.m_w, volColor.m_h, volColor.m_d,
                 volColor.m_nVolumeGridRes,volColor.m_bbox);

  hvolcolor.CopyAndInitFrom(volColor);

  SaveMeshGridSepreate<T,TColor>(filename, hvol, hvolcolor);
}

}
