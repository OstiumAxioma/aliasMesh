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

    // 类函数1：传入 NIfTI 掩码图像指针，根据掩码生成每个 label 对应的 PolyData
    void BuildFromMask(vtkImageData* maskImage);

    // 类函数2：传入储存路径，导出为一个 STL 网格
    // “同一个 NIfTI 文件的掩码就储存为同一个 STL”
    bool ExportToStl(const std::string& filePath) const;

    const std::vector<MeshObject>& GetMeshes() const { return Meshes; }

    bool ExportToVTP(const std::string& filePath) const;
private:
    std::vector<MeshObject> Meshes;
};