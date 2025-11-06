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
#include <vtkWindowedSincPolyDataFilter.h>
#include <vtkDecimatePro.h>

#include <set>
#include <vector>
#include <iostream>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>

MaskReMesh::MaskReMesh() {}
MaskReMesh::~MaskReMesh() {}

void MaskReMesh::BuildFromMask(vtkImageData* maskImage)
{
    BuildFromMask(maskImage, 0); // 0 = 自动线程数
}

void MaskReMesh::BuildFromMask(vtkImageData* maskImage, unsigned int numThreadsOverride)
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

    set<int> labelSet;
    for (vtkIdType i = 0; i < numTuples; ++i) {
        int label = static_cast<int>(scalars->GetTuple1(i));
        if (label > 0) labelSet.insert(label);
    }

    if (labelSet.empty()) {
        cerr << "[MaskReMesh] No positive labels found in mask." << endl;
        return;
    }

    double origin[3] = { 0.0, 0.0, 0.0 };
    maskImage->GetOrigin(origin);

    vector<int> labels(labelSet.begin(), labelSet.end());
    mutex meshesMutex, logMutex;
    atomic<size_t> nextIndex(0);

    unsigned int hw = std::thread::hardware_concurrency();
    unsigned int numThreads = 0;

    // 判断是否用户指定
    bool userOverride = (numThreadsOverride > 0);

    if (userOverride) {
        numThreads = numThreadsOverride;
    } else {
        numThreads = (hw > 0 ? hw : 4);
    }

    // 限制不超过标签数
    numThreads = std::min<unsigned int>(numThreads, static_cast<unsigned int>(labels.size()));
    if (numThreads == 0) numThreads = 1;

    // 输出详细信息
    std::cout << "[MaskReMesh] Detected CPU cores: " 
            << (hw > 0 ? std::to_string(hw) : "unknown")
            << ", thread count set to: " << numThreads
            << (userOverride ? " (user override)" : " (auto-detected)")
            << std::endl;
    std::cout << "[MaskReMesh] Processing " << labels.size() << " label(s)..." << std::endl;


    auto globalStart = high_resolution_clock::now();

    auto worker = [&](unsigned int workerId)
    {
        while (true) {
            size_t idx = nextIndex.fetch_add(1);
            if (idx >= labels.size()) break;

            int label = labels[idx];
            auto start = high_resolution_clock::now();

            {
                lock_guard<mutex> lock(logMutex);
                cout << "[T" << workerId << "] Start label " << label << endl;
            }

            vtkSmartPointer<vtkImageThreshold> threshold = vtkSmartPointer<vtkImageThreshold>::New();
            threshold->SetInputData(maskImage);
            threshold->ThresholdBetween(label, label);
            threshold->SetInValue(1);
            threshold->SetOutValue(0);
            threshold->SetOutputScalarTypeToUnsignedChar();
            threshold->Update();

            vtkSmartPointer<vtkMarchingCubes> mc = vtkSmartPointer<vtkMarchingCubes>::New();
            mc->SetInputConnection(threshold->GetOutputPort());
            mc->SetValue(0, 0.5);
            mc->ComputeNormalsOn();
            mc->Update();

            vtkSmartPointer<vtkWindowedSincPolyDataFilter> smoother =
                vtkSmartPointer<vtkWindowedSincPolyDataFilter>::New();
            smoother->SetInputConnection(mc->GetOutputPort());
            smoother->SetNumberOfIterations(20);     // 平滑迭代次数（可调）
            smoother->BoundarySmoothingOff();        // 不平滑边界
            smoother->FeatureEdgeSmoothingOff();     // 保留特征边
            smoother->SetFeatureAngle(120.0);        // 角度阈值
            smoother->SetPassBand(0.1);              // 控制保留细节程度，越小越平滑
            smoother->NormalizeCoordinatesOn();      // 防止过度平滑导致网格塌陷
            smoother->Update();

            vtkSmartPointer<vtkPolyData> surface =
                vtkSmartPointer<vtkPolyData>::New();

            vtkSmartPointer<vtkDecimatePro> decimate =
                vtkSmartPointer<vtkDecimatePro>::New();
            decimate->SetInputConnection(smoother->GetOutputPort());
            decimate->SetTargetReduction(0.05); // 保留 95% 三角形
            decimate->PreserveTopologyOn();
            decimate->Update();

            surface->ShallowCopy(decimate->GetOutput());

            if (surface->GetNumberOfPoints() == 0) {
                auto end = high_resolution_clock::now();
                double t = duration_cast<duration<double>>(end - start).count();
                lock_guard<mutex> lock(logMutex);
                cout << "[T" << workerId << "] Label " << label << " empty (" << t << " s)" << endl;
                continue;
            }

            vtkIdType numCells = surface->GetNumberOfCells();
            vtkSmartPointer<vtkIntArray> labelArray = vtkSmartPointer<vtkIntArray>::New();
            labelArray->SetName("Label");
            labelArray->SetNumberOfTuples(numCells);
            for (vtkIdType c = 0; c < numCells; ++c)
                labelArray->SetValue(c, label);
            surface->GetCellData()->AddArray(labelArray);
            surface->GetCellData()->SetActiveScalars("Label");

            MeshObject obj{ label, surface, {origin[0], origin[1], origin[2]} };
            {
                lock_guard<mutex> lock(meshesMutex);
                Meshes.push_back(obj);
            }

            auto end = high_resolution_clock::now();
            double t = duration_cast<duration<double>>(end - start).count();
            lock_guard<mutex> lock(logMutex);
            cout << "[T" << workerId << "] Label " << label << " done (" << t << " s)" << endl;
        }
    };

    vector<thread> threads;
    for (unsigned int i = 0; i < numThreads; ++i)
        threads.emplace_back(worker, i);
    for (auto& t : threads)
        t.join();

    auto globalEnd = high_resolution_clock::now();
    double total = duration_cast<duration<double>>(globalEnd - globalStart).count();
    cout << "[MaskReMesh] Total " << Meshes.size() << " labels processed in "
         << total << " s" << endl;
}

bool MaskReMesh::ExportToStl(const std::string& filePath) const
{
    if (Meshes.empty()) return false;
    vtkSmartPointer<vtkAppendPolyData> append = vtkSmartPointer<vtkAppendPolyData>::New();
    for (const auto& m : Meshes)
        if (m.polyData) append->AddInputData(m.polyData);
    append->Update();
    auto writer = vtkSmartPointer<vtkSTLWriter>::New();
    writer->SetFileName(filePath.c_str());
    writer->SetInputConnection(append->GetOutputPort());
    writer->SetFileTypeToBinary();
    writer->Write();
    return true;
}

bool MaskReMesh::ExportToVTP(const std::string& filePath) const
{
    if (Meshes.empty()) return false;
    vtkSmartPointer<vtkAppendPolyData> append = vtkSmartPointer<vtkAppendPolyData>::New();
    for (const auto& m : Meshes)
        if (m.polyData) append->AddInputData(m.polyData);
    append->Update();
    auto writer = vtkSmartPointer<vtkXMLPolyDataWriter>::New();
    writer->SetFileName(filePath.c_str());
    writer->SetInputConnection(append->GetOutputPort());
    writer->SetDataModeToBinary();
    writer->EncodeAppendedDataOff();
    writer->Write();
    std::cout << "[MaskReMesh] Exported VTP with "
              << Meshes.size() << " label meshes -> " << filePath << std::endl;
    return true;
}
