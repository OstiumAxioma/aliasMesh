// header/MaskReMesh.h
#pragma once

#include <vector>
#include <string>
#include <vtkSmartPointer.h>

class vtkImageData;
class vtkPolyData;

class MaskReMesh
{
public:
    struct MeshObject
    {
        int label;                                  // 掩码 indice / label
        vtkSmartPointer<vtkPolyData> polyData;      // 当前标签的网格
        double origin[3];                           // 从 NIfTI 中读取的 Origin
    };

    MaskReMesh();
    ~MaskReMesh();

    // 自动线程数版本
    void BuildFromMask(vtkImageData* maskImage);

    // 可自定义线程数版本（numThreads=1 表示单线程，=0 表示自动）
    void BuildFromMask(vtkImageData* maskImage, unsigned int numThreadsOverride);

    bool ExportToStl(const std::string& filePath) const;
    bool ExportToVTP(const std::string& filePath) const;

    const std::vector<MeshObject>& GetMeshes() const { return Meshes; }

private:
    std::vector<MeshObject> Meshes;
};