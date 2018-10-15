#include <QFileDialog>
#include "LVRReconstructionMarchingCubesDialog.hpp"

#include <lvr2/algorithm/NormalAlgorithms.hpp>

#include <lvr2/reconstruction/AdaptiveKSearchSurface.hpp>
#include <lvr2/reconstruction/BilinearFastBox.hpp>
#include <lvr2/reconstruction/FastReconstruction.hpp>
#include <lvr2/reconstruction/FastBox.hpp>
#include <lvr2/reconstruction/FastBox.hpp>
#include <lvr2/reconstruction/PointsetGrid.hpp>
#include <lvr2/reconstruction/TetraederBox.hpp>

#include <lvr2/geometry/HalfEdgeMesh.hpp>

#include <lvr2/io/PointBuffer.hpp>
#include <lvr2/io/Progress.hpp>

namespace lvr2
{

//QProgressDialog* LVRReconstructViaMarchingCubesDialog::m_progressBar = new QProgressDialog;
LVRReconstructViaMarchingCubesDialog* LVRReconstructViaMarchingCubesDialog::m_master; 
void LVRReconstructViaMarchingCubesDialog::updateProgressbar(int p)
{
	m_master->setProgressValue(p);
}

void LVRReconstructViaMarchingCubesDialog::updateProgressbarTitle(string t)
{
	m_master->setProgressTitle(t);
}


void LVRReconstructViaMarchingCubesDialog::setProgressValue(int v)
{
	Q_EMIT(progressValueChanged(v));
    QApplication::processEvents();
}

void LVRReconstructViaMarchingCubesDialog::setProgressTitle(string t)
{
	Q_EMIT(progressTitleChanged(QString(t.c_str())));
    QApplication::processEvents();
}

LVRReconstructViaMarchingCubesDialog::LVRReconstructViaMarchingCubesDialog(string decomposition, LVRPointCloudItem* pc, LVRModelItem* parent, QTreeWidget* treeWidget, vtkRenderWindow* window) :
   m_decomposition(decomposition), 
   m_pc(pc), m_parent(parent), 
   m_treeWidget(treeWidget),
   m_renderWindow(window)
{
	m_master = this;

    // Setup DialogUI and events
    QDialog* dialog = new QDialog(m_treeWidget);
    m_dialog = new ReconstructViaMarchingCubesDialog;
    m_dialog->setupUi(dialog);

    if(decomposition == "PMC")
    {
    	dialog->setWindowTitle("Planar Marching Cubes");
    }

    connectSignalsAndSlots();

    m_progressDialog = new QProgressDialog;
    m_progressDialog->setMinimum(0);
    m_progressDialog->setMaximum(100);
    m_progressDialog->setMinimumDuration(100);
    m_progressDialog->setWindowTitle("Processing...");
    m_progressDialog->setAutoClose(false);

    // Register LVR progress callbacks
    ProgressBar::setProgressCallback(&updateProgressbar);
    ProgressBar::setProgressTitleCallback(&updateProgressbarTitle);

    connect(this, SIGNAL(progressValueChanged(int)), m_progressDialog, SLOT(setValue(int)));
    connect(this, SIGNAL(progressTitleChanged(const QString&)), m_progressDialog, SLOT(setLabelText(const QString&)));

    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

LVRReconstructViaMarchingCubesDialog::~LVRReconstructViaMarchingCubesDialog()
{
    m_master = 0;
    ProgressBar::setProgressCallback(0);
    ProgressBar::setProgressTitleCallback(0);
}

void LVRReconstructViaMarchingCubesDialog::connectSignalsAndSlots()
{
    QObject::connect(m_dialog->comboBox_pcm, SIGNAL(currentIndexChanged(const QString)), this, SLOT(toggleRANSACcheckBox(const QString)));
    QObject::connect(m_dialog->comboBox_gs, SIGNAL(currentIndexChanged(int)), this, SLOT(switchGridSizeDetermination(int)));
    QObject::connect(m_dialog->buttonBox, SIGNAL(accepted()), this, SLOT(generateMesh()));
}

void LVRReconstructViaMarchingCubesDialog::toggleRANSACcheckBox(const QString &text)
{
    QCheckBox* ransac_box = m_dialog->checkBox_RANSAC;
    if(text == "PCL")
    {
        ransac_box->setChecked(false);
        ransac_box->setCheckable(false);
    }
    else
    {
        ransac_box->setCheckable(true);
    }
}

void LVRReconstructViaMarchingCubesDialog::switchGridSizeDetermination(int index)
{
    QComboBox* gs_box = m_dialog->comboBox_gs;

    QLabel* label = m_dialog->label_below_gs;
    QDoubleSpinBox* spinBox = m_dialog->spinBox_below_gs;

    // TODO: Add reasonable default values
    if(index == 0)
    {
        label->setText("Voxel size");
        spinBox->setMinimum(0);
        spinBox->setMaximum(2000000);
        spinBox->setSingleStep(1);
        spinBox->setValue(10);
    }
    else
    {
        label->setText("Number of intersections");
        spinBox->setMinimum(1);
        spinBox->setMaximum(200000);
        spinBox->setSingleStep(1);
        spinBox->setValue(10);
    }
}

void LVRReconstructViaMarchingCubesDialog::generateMesh()
{
    QComboBox* pcm_box = m_dialog->comboBox_pcm;
    string pcm = pcm_box->currentText().toStdString();
    QCheckBox* extrusion_box = m_dialog->checkBox_Extrusion;
    bool extrusion = extrusion_box->isChecked();
    QCheckBox* ransac_box = m_dialog->checkBox_RANSAC;
    bool ransac = ransac_box->isChecked();
    QSpinBox* kn_box = m_dialog->spinBox_kn;
    int kn = kn_box->value();
    QSpinBox* kd_box = m_dialog->spinBox_kd;
    int kd = kd_box->value();
    QSpinBox* ki_box = m_dialog->spinBox_ki;
    int ki = ki_box->value();
    QCheckBox* reNormals_box = m_dialog->checkBox_renormals;
    bool reestimateNormals = reNormals_box->isChecked();
    QComboBox* gridMode_box = m_dialog->comboBox_gs;
    bool useVoxelsize = (gridMode_box->currentIndex() == 0) ? true : false;
    QDoubleSpinBox* gridSize_box = m_dialog->spinBox_below_gs;
    float  resolution = (float)gridSize_box->value();


    m_progressDialog->raise();
    m_progressDialog->show();
    m_progressDialog->activateWindow();

    using Vec = BaseVector<float>;

    PointBufferPtr pc_buffer = m_pc->getPointBuffer();

    PointsetSurfacePtr<Vec> surface;

    if(pcm == "STANN" || pcm == "FLANN" || pcm == "NABO" || pcm == "NANOFLANN")
    {
        surface = PointsetSurfacePtr<Vec>( new AdaptiveKSearchSurface<Vec>(pc_buffer, pcm, kn, ki, kd, (ransac ? 1 : 0)) );
    }

    if(!surface->pointBuffer()->hasNormals() || reestimateNormals)
    {
        surface->calculateSurfaceNormals();
    }

    HalfEdgeMesh<Vec> mesh;

    if(m_decomposition == "MC")
    {
        auto grid = std::make_shared<PointsetGrid<Vec, FastBox<Vec>>>(
            resolution,
            surface,
            surface->getBoundingBox(),
            useVoxelsize,
            extrusion
        );
        grid->calcDistanceValues();
        auto reconstruction = make_unique<FastReconstruction<Vec, FastBox<Vec>>>(grid);
        reconstruction->getMesh(mesh);
    }
    else if(m_decomposition == "PMC")
    {
        BilinearFastBox<Vec>::m_surface = surface;
        auto grid = std::make_shared<PointsetGrid<Vec, BilinearFastBox<Vec>>>(
            resolution,
            surface,
            surface->getBoundingBox(),
            useVoxelsize,
            extrusion
        );
        grid->calcDistanceValues();
        auto reconstruction = make_unique<FastReconstruction<Vec, BilinearFastBox<Vec>>>(grid);
        reconstruction->getMesh(mesh);
    }
    else if(m_decomposition == "MT")
    {
        auto grid = std::make_shared<PointsetGrid<Vec, TetraederBox<Vec>>>(
            resolution,
            surface,
            surface->getBoundingBox(),
            useVoxelsize,
            extrusion
        );
        grid->calcDistanceValues();
        auto reconstruction = make_unique<FastReconstruction<Vec, TetraederBox<Vec>>>(grid);
        reconstruction->getMesh(mesh);
    }
    else if(m_decomposition == "SF")
    {
        SharpBox<Vec>::m_surface = surface;
        auto grid = std::make_shared<PointsetGrid<Vec, SharpBox<Vec>>>(
            resolution,
            surface,
            surface->getBoundingBox(),
            useVoxelsize,
            extrusion
        );
        grid->calcDistanceValues();
        auto reconstruction = make_unique<FastReconstruction<Vec, SharpBox<Vec>>>(grid);
        reconstruction->getMesh(mesh);
    }

    auto faceNormals = calcFaceNormals(mesh);

    ClusterBiMap<FaceHandle> clusterBiMap = planarClusterGrowing(mesh, faceNormals, 0.85);
    deleteSmallPlanarCluster(mesh, clusterBiMap, 10);

    ClusterPainter painter(clusterBiMap);
    auto clusterColors = DenseClusterMap<Rgb8Color>(painter.simpsons(mesh));
    auto vertexNormals = calcVertexNormals(mesh, faceNormals, *surface);

    TextureFinalizer<Vec> finalize(clusterBiMap);
    finalize.setVertexNormals(vertexNormals);
    finalize.setClusterColors(clusterColors);
    Materializer<Vec> materializer(mesh, clusterBiMap, faceNormals, *surface);
    MaterializerResult<Vec> matResult = materializer.generateMaterials();
    finalize.setMaterializerResult(matResult);
    MeshBufferPtr buffer = finalize.apply(mesh);

    ModelPtr model(new Model(buffer));
	ModelBridgePtr bridge(new LVRModelBridge(model));
	
	vtkSmartPointer<vtkRenderer> renderer = m_renderWindow->GetRenderers()->GetFirstRenderer();
	bridge->addActors(renderer);
   
	QString base = m_parent->getName() + " (mesh)";
    m_generatedModel = new LVRModelItem(bridge, base);

    m_treeWidget->addTopLevelItem(m_generatedModel);
    m_generatedModel->setExpanded(true);

    m_progressDialog->hide();
}

} // namespace lvr2
