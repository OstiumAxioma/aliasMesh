// src/MaskReMesh.cpp
#include "MaskReMesh.h"

#include <vtkImageData.h>
#include <vtkPointData.h>
#include <vtkDataArray.h>
#include <vtkImageThreshold.h>
#include <vtkMarchingCubes.h>
#include <vtkPolyData.h>
#include <vtkAppendPolyData.h>
#include <vtkSTLWriter.h>
#include <vtkIntArray.h>
#include <vtkCellData.h>
#include <vtkXMLPolyDataWriter.h>

#include <set>
#include <vector>
#include <iostream>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>

MaskReMesh::MaskReMesh() {}
MaskReMesh::~MaskReMesh() {}

void MaskReMesh::BuildFromMask(vtkImageData* maskImage)
{
    using namespace std;
    using namespace std::chrono;

    Meshes.clear();

    if (!maskImage) {
        cerr << "[MaskReMesh] maskImage is nullptr." << endl;
        return;
    }

    vtkPointData* pointData = maskImage->GetPointData();
    if (!pointData) {
        cerr << "[MaskReMesh] No point data in image." << endl;
        return;
    }

    vtkDataArray* scalars = pointData->GetScalars();
    if (!scalars) {
        cerr << "[MaskReMesh] No scalar array in image." << endl;
        return;
    }

    vtkIdType numTuples = scalars->GetNumberOfTuples();
    if (numTuples == 0) {
        cerr << "[MaskReMesh] Empty scalar array." << endl;
        return;
    }

    // 收集所有出现过的正整数 label
    set<int> labelSet;
    for (vtkIdType i = 0; i < numTuples; ++i) {
        double v = scalars->GetTuple1(i);
        int label = static_cast<int>(v);
        if (label > 0) {
            labelSet.insert(label);
        }
    }

    if (labelSet.empty()) {
        cerr << "[MaskReMesh] No positive labels found in mask." << endl;
        return;
    }

    double origin[3] = { 0.0, 0.0, 0.0 };
    maskImage->GetOrigin(origin);

    cout << "[MaskReMesh] Found " << labelSet.size() << " labels." << endl;

    // 把 set 拷贝成 vector，方便按 index 分发任务
    vector<int> labels(labelSet.begin(), labelSet.end());

    // 线程安全地写入 Meshes & 输出 log
    mutex meshesMutex;
    mutex logMutex;

    // 全局任务索引
    atomic<size_t> nextIndex(0);

    // 线程数：不小于 1
    unsigned int hw = thread::hardware_concurrency();
    unsigned int numThreads = hw > 0 ? hw : 4;
    if (numThreads > labels.size()) {
        numThreads = static_cast<unsigned int>(labels.size());
    }
    if (numThreads == 0) {
        // 理论上不会到这里，因为上面已判断 labelSet.empty()
        return;
    }

    cout << "[MaskReMesh] Using " << numThreads
         << " worker threads for " << labels.size() << " labels." << endl;

    // 总计时开始
    auto globalStart = high_resolution_clock::now();

    auto worker = [&](unsigned int workerId)
    {
        while (true) {
            size_t idx = nextIndex.fetch_add(1);
            if (idx >= labels.size()) {
                break; // 没有更多任务
            }

            int label = labels[idx];

            auto start = high_resolution_clock::now();

            {
                lock_guard<mutex> lock(logMutex);
                cout << "[MaskReMesh][T" << workerId
                     << "] Processing label = " << label << endl;
            }

            // === threshold + marching cubes ===
            vtkSmartPointer<vtkImageThreshold> threshold =
                vtkSmartPointer<vtkImageThreshold>::New();
            threshold->SetInputData(maskImage);
            threshold->ThresholdBetween(label, label);
            threshold->SetInValue(1);
            threshold->SetOutValue(0);
            threshold->SetOutputScalarTypeToUnsignedChar();
            threshold->Update();

            vtkSmartPointer<vtkMarchingCubes> mc =
                vtkSmartPointer<vtkMarchingCubes>::New();
            mc->SetInputConnection(threshold->GetOutputPort());
            mc->SetValue(0, 0.5);
            mc->ComputeNormalsOn();
            mc->Update();

            vtkSmartPointer<vtkPolyData> surface =
                vtkSmartPointer<vtkPolyData>::New();
            surface->ShallowCopy(mc->GetOutput());

            if (surface->GetNumberOfPoints() == 0) {
                auto end = high_resolution_clock::now();
                double seconds = duration_cast<duration<double>>(end - start).count();
                lock_guard<mutex> lock(logMutex);
                cout << "[MaskReMesh][T" << workerId
                     << "] Label " << label
                     << " produced empty surface, skip. (" << seconds << " s)"
                     << endl;
                continue;
            }

            vtkIdType numCells = surface->GetNumberOfCells();
            vtkSmartPointer<vtkIntArray> labelArray =
                vtkSmartPointer<vtkIntArray>::New();
            labelArray->SetName("Label");
            labelArray->SetNumberOfComponents(1);
            labelArray->SetNumberOfTuples(numCells);
            for (vtkIdType c = 0; c < numCells; ++c) {
                labelArray->SetValue(c, label);
            }
            surface->GetCellData()->AddArray(labelArray);
            surface->GetCellData()->SetActiveScalars("Label");

            MeshObject obj;
            obj.label = label;
            obj.polyData = surface;
            obj.origin[0] = origin[0];
            obj.origin[1] = origin[1];
            obj.origin[2] = origin[2];

            // 把结果塞进 Meshes，需要加锁
            {
                lock_guard<mutex> lock(meshesMutex);
                Meshes.push_back(obj);
            }

            auto end = high_resolution_clock::now();
            double seconds = duration_cast<duration<double>>(end - start).count();
            {
                lock_guard<mutex> lock(logMutex);
                cout << "[MaskReMesh][T" << workerId
                     << "] Label " << label
                     << " finished in " << seconds << " s" << endl;
            }
        }
    };

    // 启动线程
    vector<thread> workers;
    workers.reserve(numThreads);
    for (unsigned int i = 0; i < numThreads; ++i) {
        workers.emplace_back(worker, i);
    }

    // 等待全部线程结束
    for (auto& th : workers) {
        if (th.joinable()) {
            th.join();
        }
    }

    // 总耗时
    auto globalEnd = high_resolution_clock::now();
    double totalSeconds =
        duration_cast<duration<double>>(globalEnd - globalStart).count();

    // Meshes 的顺序可能与 labelSet 遍历顺序不同（多线程插入），但每个 MeshObject 的 label 正确
    {
        lock_guard<mutex> lock(logMutex);
        cout << "[MaskReMesh] Total " << Meshes.size()
             << " labels processed in " << totalSeconds << " s" << endl;
    }
}

bool MaskReMesh::ExportToStl(const std::string& filePath) const
{
    using namespace std;

    if (Meshes.empty()) {
        cerr << "[MaskReMesh] No meshes to export." << endl;
        return false;
    }

    vtkSmartPointer<vtkAppendPolyData> append =
        vtkSmartPointer<vtkAppendPolyData>::New();

    for (size_t i = 0; i < Meshes.size(); ++i) {
        if (!Meshes[i].polyData)
            continue;
        append->AddInputData(Meshes[i].polyData);
    }
    append->Update();

    vtkSmartPointer<vtkSTLWriter> writer =
        vtkSmartPointer<vtkSTLWriter>::New();
    writer->SetFileName(filePath.c_str());
    writer->SetInputConnection(append->GetOutputPort());
    writer->SetFileTypeToBinary();
    writer->Write();

    return true;
}

bool MaskReMesh::ExportToVTP(const std::string& filePath) const
{
    using namespace std;

    if (Meshes.empty()) {
        cerr << "[MaskReMesh] No meshes to export." << endl;
        return false;
    }

    vtkSmartPointer<vtkAppendPolyData> append =
        vtkSmartPointer<vtkAppendPolyData>::New();

    for (size_t i = 0; i < Meshes.size(); ++i)
    {
        if (!Meshes[i].polyData)
            continue;
        append->AddInputData(Meshes[i].polyData);
    }
    append->Update();

    vtkSmartPointer<vtkXMLPolyDataWriter> writer =
        vtkSmartPointer<vtkXMLPolyDataWriter>::New();
    writer->SetFileName(filePath.c_str());
    writer->SetInputConnection(append->GetOutputPort());
    writer->SetDataModeToBinary();
    writer->EncodeAppendedDataOff();
    writer->Write();

    cout << "[MaskReMesh] Exported VTP with "
         << Meshes.size() << " label meshes -> "
         << filePath << endl;

    return true;
}