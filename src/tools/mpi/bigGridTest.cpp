#include <iostream>
#include <string>
#include <lvr/io/Model.hpp>
#include "BigGrid.hpp"
#include "lvr/io/DataStruct.hpp"
#include "lvr/io/PointBuffer.hpp"
#include "lvr/io/Model.hpp"
#include "lvr/io/PLYIO.hpp"
#include "lvr/geometry/BoundingBox.hpp"
#include "BigGridKdTree.hpp"
#include <fstream>
#include <sstream>
#include <lvr/io/Timestamp.hpp>
#include <lvr/reconstruction/PointsetSurface.hpp>
#include <lvr/geometry/ColorVertex.hpp>
#include <lvr/reconstruction/AdaptiveKSearchSurface.hpp>
#include <lvr/reconstruction/SearchTreeFlann.hpp>
#include <lvr/geometry/ColorVertex.hpp>
#include <lvr/geometry/Normal.hpp>
#include <lvr/reconstruction/HashGrid.hpp>
#include <lvr/reconstruction/FastReconstruction.hpp>
#include <lvr/reconstruction/PointsetGrid.hpp>
#include <lvr/geometry/QuadricVertexCosts.hpp>
#include <lvr/reconstruction/FastBox.hpp>
#include <lvr/geometry/HalfEdgeMesh.hpp>
#include "lvr/reconstruction/QueryPoint.hpp"
#include <fstream>
#include <sstream>
#include "LargeScaleOptions.hpp"
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/algorithm/string/replace.hpp>
#include "LineReader.hpp"
#include "VolumenGrid.h"
#include "BigVolumen.hpp"
#include "sortPoint.hpp"
#include <flann/flann.hpp>
#include <random>

#include <lvr2/io/PointBuffer.hpp>
#include <lvr2/geometry/BaseVector.hpp>
#include <lvr2/reconstruction/PointsetSurface.hpp>
#include <lvr2/reconstruction/AdaptiveKSearchSurface.hpp>
#include <lvr2/geometry/HalfEdgeMesh.hpp>
#include <lvr2/reconstruction/HashGrid.hpp>
#include <lvr2/reconstruction/FastReconstruction.hpp>
#include <lvr2/reconstruction/FastBox.hpp>
#include <lvr2/reconstruction/PointsetGrid.hpp>
#include <lvr2/geometry/BoundingBox.hpp>
#include <lvr2/algorithm/NormalAlgorithms.hpp>
#include <lvr2/algorithm/FinalizeAlgorithms.hpp>

using std::cout;
using std::endl;

#if defined CUDA_FOUND
    #define GPU_FOUND

    #include <lvr/reconstruction/cuda/CudaSurface.hpp>

    typedef CudaSurface GpuSurface;
#elif defined OPENCL_FOUND
    #define GPU_FOUND

    #include <lvr/reconstruction/opencl/ClSurface.hpp>
    typedef ClSurface GpuSurface;
#endif

struct duplicateVertex{
    float x;
    float y;
    float z;
    unsigned int id;

};


void getBoundingBoxNumPoints(LineReader & lr, lvr::BoundingBox<Vertexf> & bb, size_t & numPoints)
{
    while(lr.ok())
    {
        size_t rsize;
        if(lr.getFileType() == XYZNRGB)
        {
            boost::shared_ptr<xyznc> a = boost::static_pointer_cast<xyznc> (lr.getNextPoints(rsize,1024));
            for(int i = 0 ; i< rsize ; i++)
            {
                bb.expand(a.get()[i].point.x,a.get()[i].point.y,a.get()[i].point.z);
            }
            numPoints+=rsize;
        }
        else if(lr.getFileType() == XYZN)
        {
            boost::shared_ptr<xyzn> a = boost::static_pointer_cast<xyzn> (lr.getNextPoints(rsize,1024));
            for(int i = 0 ; i< rsize ; i++)
            {
                bb.expand(a.get()[i].point.x,a.get()[i].point.y,a.get()[i].point.z);
            }
            numPoints+=rsize;
        }
        else if(lr.getFileType() == XYZ)
        {
            boost::shared_ptr<xyz> a = boost::static_pointer_cast<xyz> (lr.getNextPoints(rsize,1024));
            for(int i = 0 ; i< rsize ; i++)
            {
                bb.expand(a.get()[i].point.x,a.get()[i].point.y,a.get()[i].point.z);
            }
            numPoints+=rsize;
        }
        else if(lr.getFileType() == XYZRGB)
        {
            boost::shared_ptr<xyzc> a = boost::static_pointer_cast<xyzc> (lr.getNextPoints(rsize,1024));
            for(int i = 0 ; i< rsize ; i++)
            {
                bb.expand(a.get()[i].point.x,a.get()[i].point.y,a.get()[i].point.z);
            }
            numPoints+=rsize;
        }

    }

}

using Vec = lvr2::BaseVector<float>;

typedef lvr::PointsetSurface<lvr::ColorVertex<float, unsigned char> > psSurface;
typedef lvr::AdaptiveKSearchSurface<lvr::ColorVertex<float, unsigned char>, lvr::Normal<float> > akSurface;

int main(int argc, char** argv)
{

//make sure to include the random number generators and such

//    std::random_device seeder;
//    std::mt19937 engine(seeder());
//    std::uniform_real_distribution<float> dist(10, 10.05);
//    std::random_device seeder2;
//    std::mt19937 engine2(seeder());
//    std::uniform_real_distribution<float> dist2(0, 10);
//    ofstream testofs("plane.pts");
//    for(float x = 0; x < 10; x+=0.05)
//    {
//        for(float y = 0 ; y < 10 ; y+=0.05)
//        {
//            testofs << dist2(engine2) << " " << dist2(engine2) << " " << dist(engine) << endl;
//        }
//    }

    double datastruct_time = 0;
    double normal_time = 0;
    double dist_time = 0;
    double mesh_time = 0;
    double merge_time = 0;
    double dup_time = 0;

    LargeScaleOptions::Options options(argc, argv);
    double seconds=0;
    size_t bufferSize = options.getLineReaderBuffer();

    std::vector <string> inputFiles = options.getInputFileName();
    std::string firstPath = options.getInputFileName()[0];
    bool gotSerializedBG = false;

    float voxelsize = options.getVoxelsize();
    float scale = options.getScaling();
    std::vector<float> flipPoint = options.getFlippoint();
    cout << lvr::timestamp << "Starting grid" << endl;
    float volumenSize = (float)(options.getVolumenSize()); // 10 x 10 x 10 voxel
    boost::shared_ptr<BigGrid> bg;
    boost::shared_ptr<BigVolumen> bv;
    lvr::BoundingBox<lvr::Vertexf> bb;
    if(firstPath.find(".ls") != std::string::npos)
    {
        gotSerializedBG = true;
        volumenSize = 0;
    }
    if(volumenSize <= 0)
    {
        double start_ss = lvr::timestamp.getElapsedTimeInS();
        if(gotSerializedBG)
        {
            bg = boost::shared_ptr<BigGrid>(new BigGrid(firstPath));

        }
        else
        {
            bg = boost::shared_ptr<BigGrid>(new BigGrid(inputFiles, voxelsize, scale, bufferSize));
            bg->serialize("serinfo.ls");

        }
        double end_ss = lvr::timestamp.getElapsedTimeInS();
        seconds+=(end_ss - start_ss);
        cout << lvr::timestamp << "grid finished in" << (end_ss - start_ss) << "sec." << endl;
        bb = bg->getBB();
        cout << bb << endl;
        double end_ss2 = lvr::timestamp.getElapsedTimeInS();
        datastruct_time = (end_ss2 - start_ss);
    }



    vector<BoundingBox<Vertexf > > partitionBoxes;
    //lvr::floatArr points = bg->getPointCloud(numPoints);
    cout << lvr::timestamp << "Making tree" << endl;
    std::unordered_map<size_t, BigVolumen::VolumeCellInfo >* cells;
    std::vector<BigVolumen::VolumeCellInfo*> cell_vec;
    if(volumenSize > 0)
    {
        if(fmod(volumenSize, voxelsize) > 0.00001 )
        {
            cerr << "ERROR: Size of Volume must be multiple of voxelsize e.g. Volume 12 and voxelsize 2" << endl;
            exit(-1);
        }
        cout << lvr::timestamp << " getting BoundingBox" << endl;


        LineReader lr(inputFiles);
        bv = boost::shared_ptr<BigVolumen>(new BigVolumen(inputFiles, volumenSize, volumenSize/10) );
        cells = bv->getCellinfo();
        for(auto cell_it = cells->begin(); cell_it != cells->end(); cell_it++)
        {
            cell_vec.push_back(&cell_it->second);
            BoundingBox<Vertexf > partBB = cell_it->second.bb;
            partitionBoxes.push_back(partBB);
        }
    }
    else
    {
        if(gotSerializedBG)
        {
            ifstream partBoxIFS("KdTree.ser");
            while(partBoxIFS.good())
            {
                float minx,miny,minz,maxx,maxy,maxz;
                partBoxIFS >> minx;
                partBoxIFS >> miny;
                partBoxIFS >> minz;
                partBoxIFS >> maxx;
                partBoxIFS >> maxy;
                partBoxIFS >> maxz;
                BoundingBox<Vertexf > partBB(minx,miny,minz,maxx,maxy,maxz);
                partitionBoxes.push_back(partBB);
            }
        }
        else
        {
            BigGridKdTree gridKd(bg->getBB(),options.getNodeSize(),bg.get(), voxelsize);
            gridKd.insert(bg->pointSize(),bg->getBB().getCentroid());
            ofstream partBoxOfs("KdTree.ser");
            for(size_t i = 0 ; i <  gridKd.getLeafs().size(); i++)
            {
                BoundingBox<Vertexf > partBB = gridKd.getLeafs()[i]->getBB();
                partitionBoxes.push_back(partBB);
                partBoxOfs << partBB.getMin()[0] << " " << partBB.getMin()[1] << " " << partBB.getMin()[2] << " "
                           << partBB.getMax()[0] << " " << partBB.getMax()[1] << " "<< partBB.getMax()[2] << std::endl;
            }
        }

    }

    cout << lvr::timestamp << "finished tree" << endl;

    std::cout << lvr::timestamp << "got: " << partitionBoxes.size() << " leafs, saving leafs" << std::endl;



    BoundingBox<ColorVertex<float,unsigned char> > cbb(bb.getMin().x, bb.getMin().y, bb.getMin().z,
                                                       bb.getMax().x, bb.getMax().y, bb.getMax().z);

    vector<string> grid_files;
    vector<string> normal_files;
    for(size_t i = 0 ; i < partitionBoxes.size() ; i++)
    {
        string name_id = std::to_string(i);
        size_t numPoints;

        //todo: okay?
        cout << lvr::timestamp << "loading data " << i  << endl;
        double start_s = lvr::timestamp.getElapsedTimeInS();
        lvr::floatArr points;
        lvr::ucharArr colors;
        lvr::PointBufferPtr p_loader(new lvr::PointBuffer);

        if(volumenSize > 0)
        {

            stringstream ss_name;
            ss_name << "part-" <<  cell_vec[i]->ix << "-" <<  cell_vec[i]->iy << "-" <<  cell_vec[i]->iz;
            name_id = ss_name.str();
            numPoints = cell_vec[i]->size + cell_vec[i]->overlapping_size;
            std::cout << ss_name.str() << ": " << cell_vec[i]->size << " + " << cell_vec[i]->overlapping_size << " = " << numPoints << std::endl;
            if(numPoints < options.getKd()*4 || numPoints < options.getKn()*4 || numPoints < options.getKi()*4) continue;
            stringstream ss_points;
            ss_points << "part-" <<  cell_vec[i]->ix << "-" <<  cell_vec[i]->iy << "-" <<  cell_vec[i]->iz << "-points.binary";
            cout << "file= " << ss_points.str() << endl;
            std::ifstream size_in(ss_points.str(), std::ifstream::ate | std::ifstream::binary);
            size_t file_bytes = size_in.tellg();
            cout << "OLD NUM: " << numPoints << endl;
//            numPoints = (file_bytes/ sizeof(float))/3;
            points = lvr::floatArr(new float[numPoints*3]);
            cout << "NEW NUM: " << numPoints  << endl;
            ifstream ifs_points(ss_points.str(), std::ifstream::binary);
                // ifstream ifs_points(ss_points.str());
            ifs_points.read((char*)points.get(), sizeof(float)*3*numPoints);

            size_t readNum = numPoints;

            std::stringstream ss_normals2;
            ss_normals2 << "part-" <<  cell_vec[i]->ix << "-" <<  cell_vec[i]->iy << "-" <<  cell_vec[i]->iz << "-points.ply";
            cout << "PART123: " << ss_normals2.str() << endl;
            PointBufferPtr pt(new PointBuffer);
            pt->setPointArray(points,numPoints);
            ModelPtr m(new Model(pt));
            ModelFactory::saveModel( m, ss_normals2.str());
            p_loader->setPointArray(points, numPoints);
        }
        else
        {
            points =  bg->points(partitionBoxes[i].getMin().x - voxelsize*3, partitionBoxes[i].getMin().y - voxelsize*3, partitionBoxes[i].getMin().z - voxelsize*3 ,
                                 partitionBoxes[i].getMax().x + voxelsize*3, partitionBoxes[i].getMax().y + voxelsize*3, partitionBoxes[i].getMax().z + voxelsize*3,numPoints);
            p_loader->setPointArray(points, numPoints);
            if(options.savePointNormals()  || options.onlyNormals())
            {
                if(bg->hasColors())
                {
                    size_t numColors;
                    colors = bg->colors(partitionBoxes[i].getMin().x - voxelsize*3, partitionBoxes[i].getMin().y - voxelsize*3, partitionBoxes[i].getMin().z - voxelsize*3 ,
                                        partitionBoxes[i].getMax().x + voxelsize*3, partitionBoxes[i].getMax().y + voxelsize*3, partitionBoxes[i].getMax().z + voxelsize*3,numColors);
                    cout << "got ************* " << numColors << " colors" << endl;
                    p_loader->setPointColorArray(colors, numColors);
                }

            }
        }

        double end_s = lvr::timestamp.getElapsedTimeInS();
        seconds+=(end_s - start_s);
        cout << lvr::timestamp << "finished loading data " << i  << " in " << (end_s - start_s)<< endl;
        //std::cout << "i: " << std::endl << bb << std::endl << "got : " << numPoints << std::endl;
        if(numPoints<=50) continue;

        lvr2::BoundingBox<Vec> gridbb(
            Vec(
                partitionBoxes[i].getMin().x,
                partitionBoxes[i].getMin().y,
                partitionBoxes[i].getMin().z
            ),
            Vec(
                partitionBoxes[i].getMax().x,
                partitionBoxes[i].getMax().y,
                partitionBoxes[i].getMax().z
            )
        );

        cout << "grid: " << i << "/" << partitionBoxes.size()-1 << endl;
        cout << "grid has " << numPoints << " points" << endl;
        cout << "kn=" << options.getKn() << endl;
        cout << "ki=" << options.getKi() << endl;
        cout << "kd=" << options.getKd() << endl;
        cout << gridbb << endl;

        bool navail = false;
        if(volumenSize <= 0)
        {
            if(bg->hasNormals() )
            {

                size_t numNormals;
                lvr::floatArr normals = bg->normals(partitionBoxes[i].getMin().x - voxelsize*3, partitionBoxes[i].getMin().y - voxelsize*3, partitionBoxes[i].getMin().z - voxelsize*3 ,
                                                    partitionBoxes[i].getMax().x + voxelsize*3, partitionBoxes[i].getMax().y + voxelsize*3, partitionBoxes[i].getMax().z + voxelsize*3,numNormals);


                p_loader->setPointNormalArray(normals, numNormals);
                navail = true;
            }

        }
        else
        {
            if(bv->hasNormals())
            {
                std::cout << "reading normals" << std::endl;
                size_t numNormals = numPoints;
                stringstream ss_normals;
                ss_normals << "part-" <<  cell_vec[i]->ix << "-" <<  cell_vec[i]->iy << "-" <<  cell_vec[i]->iz << "-normals.binary";
                ifstream ifs_normals(ss_normals.str(), std::ifstream::binary);
                lvr::floatArr normals(new float[numNormals*3]);
                ifs_normals.read((char*)normals.get(), sizeof(float)*3*numNormals);
                p_loader->setPointNormalArray(normals, numNormals);
                if(bv->hasNormals()) navail = true;
            }

        }

        if(navail)
        {


        } else {

            #ifdef GPU_FOUND
            if( options.useGPU() )
            {
                std::cout << "calculating normals of " << numPoints << " points" << std::endl;
                if(numPoints>30000000) std::cout << "this is a lot of points, this might fail" << std::endl;
                double normal_start = lvr::timestamp.getElapsedTimeInS();
                floatArr normals = floatArr(new float[ numPoints * 3 ]);
                cout << timestamp << "Constructing kd-tree..." << endl;
                GpuSurface gpu_surface(points, numPoints);
                cout << timestamp << "Finished kd-tree construction." << endl;
                gpu_surface.setKn( options.getKn() );
                gpu_surface.setKi( options.getKi() );
                gpu_surface.setFlippoint(flipPoint[0], flipPoint[1], flipPoint[2]);
                cout << timestamp << "Start Normal Calculation..." << endl;
                gpu_surface.calculateNormals();
                gpu_surface.getNormals(normals);
                cout << timestamp << "Finished Normal Calculation. " << endl;
                p_loader->setPointNormalArray(normals, numPoints);
                gpu_surface.freeGPU();
                double normal_end = lvr::timestamp.getElapsedTimeInS();
                normal_time+=(normal_end-normal_start);
            }
            #else
                cout << "ERROR: OpenCl not found" << endl;
//                exit(-1);
            #endif
        }

        auto buffer = make_shared<lvr2::PointBuffer<Vec>>(*p_loader);
        lvr2::PointsetSurfacePtr<Vec> surface;
        surface = make_shared<lvr2::AdaptiveKSearchSurface<Vec>>(
            buffer,
            "FLANN",
            options.getKn(),
            options.getKi(),
            options.getKd(),
            options.useRansac()
        );

        if(navail)
        {

        }
        else if(!options.useGPU()) {
            double normal_start = lvr::timestamp.getElapsedTimeInS();
            surface->calculateSurfaceNormals();
            double normal_end = lvr::timestamp.getElapsedTimeInS();
            normal_time+=(normal_end-normal_start);
        }

        if(options.savePointNormals() || options.onlyNormals())
        {
            lvr::PointBufferPtr p_normal_loader(new lvr::PointBuffer);

            vector<float> tmpPBuffer;
            vector<unsigned char> tmpCBuffer;
            vector<float> tmpNBuffer;

            size_t p_np, c_np, n_np;
            auto p_loader_old = buffer->toOldBuffer();
            auto ppoints = p_loader_old.getPointArray(p_np);
            auto pcolors = p_loader_old.getPointColorArray(c_np);
            cout << "COLOR1 " << c_np << endl;
            auto pnormals = p_loader_old.getPointNormalArray(n_np);

            lvr::BoundingBox<Vertexf> nbb;
            for(size_t p_count = 0 ; p_count < p_np ; p_count++ )
            {
                bool intersect;
                intersect =  partitionBoxes[i].contains(ppoints.get()[p_count*3], ppoints.get()[p_count*3+1], ppoints.get()[p_count*3+2]  );
                if(intersect)
                {
                    nbb.expand(ppoints.get()[p_count*3], ppoints.get()[p_count*3+1], ppoints.get()[p_count*3+2]);
                    tmpPBuffer.push_back(ppoints.get()[p_count*3]);
                    tmpPBuffer.push_back(ppoints.get()[p_count*3+1]);
                    tmpPBuffer.push_back(ppoints.get()[p_count*3+2]);

                    tmpNBuffer.push_back(pnormals.get()[p_count*3]);
                    tmpNBuffer.push_back(pnormals.get()[p_count*3+1]);
                    tmpNBuffer.push_back(pnormals.get()[p_count*3+2]);
                    if(c_np >0)
                    {
                        tmpCBuffer.push_back(pcolors.get()[p_count*3]);
                        tmpCBuffer.push_back(pcolors.get()[p_count*3+1]);
                        tmpCBuffer.push_back(pcolors.get()[p_count*3+2]);
                    }

                }
            }





            floatArr tmpPointArray(new float[tmpPBuffer.size()]);
            ucharArr tmpColorArray(new unsigned char[tmpCBuffer.size()]);
            floatArr tmpNormalArray(new float[tmpNBuffer.size()]);

            for(size_t ii = 0 ; ii < tmpPBuffer.size(); ii++)
            {
                tmpPointArray[ii] = tmpPBuffer[ii];
            }
            for(size_t ii = 0 ; ii < tmpNBuffer.size(); ii++)
            {
                tmpNormalArray[ii] = tmpNBuffer[ii];
            }
            for(size_t ii = 0 ; ii < tmpCBuffer.size(); ii++)
            {
                tmpColorArray[ii] = tmpCBuffer[ii];
            }
            cout << "NUMP: " << tmpPBuffer.size()/3 << "|" << tmpCBuffer.size()/3 << "|" << tmpNBuffer.size()/3 << endl;
            p_normal_loader->setPointArray(tmpPointArray,tmpPBuffer.size()/3);
            cout << "COLOR2: " << tmpCBuffer.size()/3 << endl;
            p_normal_loader->setPointColorArray(tmpColorArray,tmpCBuffer.size()/3);
            p_normal_loader->setPointNormalArray(tmpNormalArray,tmpNBuffer.size()/3);

            cout << "OLD NORMAL SIZE: " << p_np << " NEW NORMAL SIZE: " << tmpNBuffer.size()/3 << endl;

            std::stringstream ss_normals;
            ss_normals << name_id << "-normals.ply";
            ModelPtr m(new Model(p_normal_loader));
            ModelFactory::saveModel( m, ss_normals.str());
            normal_files.push_back(ss_normals.str());
        }

        if(options.onlyNormals()) continue;
        double grid_start = lvr::timestamp.getElapsedTimeInS();


        auto grid = std::make_shared<lvr2::PointsetGrid<Vec, lvr2::FastBox<Vec>>>(
            voxelsize,
            surface,
            gridbb,
            true,
            options.extrude()
        );
        grid->setBB(gridbb);
        grid->calcDistanceValues();
        auto reconstruction = make_unique<lvr2::FastReconstruction<Vec, lvr2::FastBox<Vec>>>(grid);

        double grid_end = lvr::timestamp.getElapsedTimeInS();
        dist_time+=(grid_end-grid_start);

        double mesh_start = lvr::timestamp.getElapsedTimeInS();
        lvr2::HalfEdgeMesh<Vec> mesh;
        cout << lvr::timestamp << " saving data " << i << endl;
        vector<unsigned int> duplicates;
        reconstruction->getMesh(mesh, grid->qp_bb, duplicates, voxelsize * 5);

        if(options.optimizePlanes())
        {
            std::stringstream ss_normals;
            ss_normals << name_id << "-normals.ply";
            ModelPtr m(new Model);
            auto oldBuffer = boost::make_shared<lvr::PointBuffer>(
                surface->pointBuffer()->toOldBuffer()
            );
            m->m_pointCloud = oldBuffer;
            ModelFactory::saveModel(m, ss_normals.str());
        }

        // Calc normals for vertices
        auto faceNormals = lvr2::calcFaceNormals(mesh);
        auto vertexNormals = lvr2::calcVertexNormals(mesh, faceNormals, *surface);

        // Finalize mesh
        lvr2::SimpleFinalizer<Vec> finalize;
        finalize.setNormalData(vertexNormals);
        auto meshBuffer = finalize.apply(mesh);

        double mesh_end = lvr::timestamp.getElapsedTimeInS();
        mesh_time += mesh_end-mesh_start;
        start_s = lvr::timestamp.getElapsedTimeInS();

        std::stringstream ss_mesh;
        ss_mesh << name_id << "-mesh.ply";
        auto m = boost::make_shared<lvr::Model>(meshBuffer->toOldBuffer());
        ModelFactory::saveModel(m, ss_mesh.str());

        std::stringstream ss_grid;
        ss_grid << name_id << "-grid.ser";
//        ps_grid->saveCells(ss_grid.str());
        grid_files.push_back(ss_grid.str());

        std::stringstream ss_duplicates;
        ss_duplicates << name_id << "-duplicates.ser";
        std::ofstream ofs(ss_duplicates.str(), std::ofstream::out | std::ofstream::trunc);
        boost::archive::text_oarchive oa(ofs);
        oa & duplicates;

        end_s = lvr::timestamp.getElapsedTimeInS();
        seconds+=(end_s - start_s);
        cout << lvr::timestamp << "finished saving data " << i << " in " << (end_s - start_s) << endl;
    }

    vector<size_t> offsets;
    offsets.push_back(0);
    vector<unsigned int> all_duplicates;
//    vector<float> duplicateVertices;
    vector<duplicateVertex> duplicateVertices;
    for(int i = 0 ; i <grid_files.size() ; i++)
    {
        string duplicate_path = grid_files[i];
        string ply_path = grid_files[i];
        double start_s = lvr::timestamp.getElapsedTimeInS();
        boost::algorithm::replace_last(duplicate_path, "-grid.ser", "-duplicates.ser");
        boost::algorithm::replace_last(ply_path, "-grid.ser", "-mesh.ply");
        std::ifstream ifs(duplicate_path);
        boost::archive::text_iarchive ia(ifs);
        std::vector<unsigned int> duplicates;
        ia & duplicates;
        cout << "MESH " << i << " has " << duplicates.size() << " possible duplicates" << endl;
        LineReader lr(ply_path);

        size_t numPoints = lr.getNumPoints();
        offsets.push_back(numPoints+offsets[i]);
        if(numPoints==0) continue;
        lvr::PLYIO io;
        ModelPtr modelPtr = io.read(ply_path);
        //ModelPtr modelPtr = ModelFactory::readModel(ply_path);
        size_t numVertices;
        floatArr modelVertices = modelPtr->m_mesh->getVertexArray(numVertices);
        double end_s = lvr::timestamp.getElapsedTimeInS();
        seconds+=(end_s - start_s);
//        for (size_t j = 0; j < numVertices; j++)
//        {
//            duplicates.push_back(static_cast<unsigned int>(j));
//        }

        for(size_t j  = 0 ; j<duplicates.size() ; j++)
        {
            duplicateVertex v;
            v.x = modelVertices[duplicates[j]*3];
            v.y = modelVertices[duplicates[j]*3+1];
            v.z = modelVertices[duplicates[j]*3+2];
            v.id = duplicates[j] + offsets[i];
            duplicateVertices.push_back(v);
        }

//        std::transform (duplicates.begin(), duplicates.end(), duplicates.begin(), [&](unsigned int x){return x+offsets[i];});
//        all_duplicates.insert(all_duplicates.end(),duplicates.begin(), duplicates.end());

    }
    std::sort(duplicateVertices.begin(), duplicateVertices.end(), [](const duplicateVertex &left, const duplicateVertex &right) {
        return left.id < right.id;
    });
//    std::sort(all_duplicates.begin(), all_duplicates.end());
    std::unordered_map<unsigned int, unsigned int> oldToNew;

    ofstream ofsd;
    ofsd.open("duplicate_colors.pts", ios_base::app);

//    flann::Matrix<float> flannPoints = flann::Matrix<float> (new float[3 * duplicateVertices.size()], duplicateVertices.size(), 3);
//    for(size_t i = 0; i < duplicateVertices.size(); i++)
//    {
//        flannPoints[i][0] = duplicateVertices[3 * i];
//        flannPoints[i][1] = duplicateVertices[3 * i + 1];
//        flannPoints[i][2] = duplicateVertices[3 * i + 2];
//    }
//    boost::shared_ptr<flann::Index<flann::L2_Simple<float> > >     tree =
//            boost::shared_ptr<flann::Index<flann::L2_Simple<float> > >(new flann::Index<flann::L2_Simple<float> >(flannPoints, ::flann::KDTreeSingleIndexParams (10, false)));
//    tree->buildIndex();
    float comp_dist = std::max(voxelsize/1000, 0.0001f);
    double dist_epsilon_squared = comp_dist*comp_dist;
    double dup_start = lvr::timestamp.getElapsedTimeInS();

//    vector<float> dupBuffer(duplicateVertices.size());
//    for(duplicateVertex & v : duplicateVertices)
//    {
//        dupBuffer.push_back(v.x);
//        dupBuffer.push_back(v.y);
//        dupBuffer.push_back(v.z);
//    }
//    if(duplicateVertices.size()>0)
//    {
//        SearchTreeFlann<Vertexf> searchTree(duplicateVertices);
//
//        for(size_t i = 0 ; i < duplicateVertices.size() ; i++)
//        {
//            vector<size_t> indices;
//            vector<float> distances;
//            searchTree.kSearch(duplicateVertices[i].x, duplicateVertices[i].y, duplicateVertices[i].z,5, indices, distances);
//            for(int j = 0 ; j <distances.size();j++)
//            {
//                if(distances[j] < comp_dist && all_duplicates[i/3] != all_duplicates[indices[j]])
//                {
//                    if(all_duplicates[i/3] == all_duplicates[indices[j]]) continue;
//                    auto find_it = oldToNew.find(all_duplicates[i/3]);
//                    if( find_it == oldToNew.end())
//                    {
//                        oldToNew[all_duplicates[indices[j]]] = all_duplicates[i/3];
////                    cout << "dup found! mapping " << all_duplicates[j/3] << " -> " << all_duplicates[i/3] << " dist: " << sqrt(dist_squared)<< endl;
//
//                    }
//                }
//            }
//        }
//    }


    string comment = lvr::timestamp.getElapsedTime() + "Removing duplicates ";
    lvr::ProgressBar progress(duplicateVertices.size(), comment);

    omp_lock_t writelock;
    omp_init_lock(&writelock);
    #pragma omp parallel for
    for(size_t i = 0 ; i < duplicateVertices.size() ; i++)
    {
        float ax = duplicateVertices[i].x;
        float ay = duplicateVertices[i].y;
        float az = duplicateVertices[i].z;
//        vector<unsigned int> duplicates_of_i;
        int found = 0;
        auto find_it = oldToNew.find(duplicateVertices[i].id);
        if( find_it == oldToNew.end())
        {
#pragma omp parallel for schedule(dynamic,1)
            for(size_t j = 0 ; j < duplicateVertices.size()  ; j++)
            {
                if(i==j || found >5) continue;
                float bx = duplicateVertices[j].x;
                float by = duplicateVertices[j].y;
                float bz = duplicateVertices[j].z;
                double dist_squared = (ax-bx)*(ax-bx) + (ay-by)*(ay-by) + (az-bz)*(az-bz);
                if(dist_squared < dist_epsilon_squared)
                {
//

                    if(duplicateVertices[j].id < duplicateVertices[i].id)
                    {
//                        cout << "FUCK THIS SHIT" << endl;
                        continue;
                    }
                    omp_set_lock(&writelock);
                    oldToNew[duplicateVertices[j].id] = duplicateVertices[i].id;
                    found++;
                    omp_unset_lock(&writelock);
//                    cout << "dup found! mapping " <<duplicateVertices[j].id << " -> " << duplicateVertices[i].id << " dist: " << sqrt(dist_squared)<< endl;

//                    ofsd << duplicateVertices[j].x << " " << duplicateVertices[j].y << " " << duplicateVertices[j].z << endl;


//                omp_unset_lock(&writelock);
                }
            }
        }
        ++progress;

    }
    cout << "FOUND: " << oldToNew.size() << " duplicates" << endl;
    double dup_end = lvr::timestamp.getElapsedTimeInS();
    dup_time+=dup_end-dup_start;
//    for(auto testit = oldToNew.begin(); testit != oldToNew.end(); testit++)
//    {
//        if(oldToNew.find(testit->second) != oldToNew.end()) cout << "SHIT FUCK SHIT" << endl;
//    }
    ofstream ofs_vertices("largeVertices.bin", std::ofstream::out | std::ofstream::trunc );
    ofstream ofs_faces("largeFaces.bin", std::ofstream::out | std::ofstream::trunc);

    size_t increment=0;
    std::map<size_t, size_t> decrements;
    decrements[0] = 0;

    size_t newNumVertices = 0;
    size_t newNumFaces = 0;
    cout << lvr::timestamp << "merging mesh..." << endl;
    for(size_t i = 0 ; i <grid_files.size() ; i++)
    {
        double start_s = lvr::timestamp.getElapsedTimeInS();

        string ply_path = grid_files[i];
        boost::algorithm::replace_last(ply_path, "-grid.ser", "-mesh.ply");
        LineReader lr(ply_path);
        size_t numPoints = lr.getNumPoints();
        if(numPoints==0) continue;
        lvr::PLYIO io;
        ModelPtr modelPtr = io.read(ply_path);
        size_t numVertices;
        size_t numFaces;
        size_t offset = offsets[i];
        floatArr modelVertices = modelPtr->m_mesh->getVertexArray(numVertices);
        uintArr modelFaces = modelPtr->m_mesh->getFaceArray(numFaces);
        double end_s = lvr::timestamp.getElapsedTimeInS();
        seconds+=(end_s - start_s);
        newNumFaces+=numFaces;
        for(size_t j = 0; j<numVertices ; j++)
        {
            float p[3];
            p[0] = modelVertices[j*3];
            p[1] = modelVertices[j*3+1];
            p[2] = modelVertices[j*3+2];
            if(oldToNew.find(j+offset)==oldToNew.end())
            {
//                ofs_vertices.write((char*)p,sizeof(float)*3);
                start_s = lvr::timestamp.getElapsedTimeInS();
                ofs_vertices << std::setprecision(16) << p[0] << " " << p[1] << " " << p[2] << endl;
                end_s = lvr::timestamp.getElapsedTimeInS();
                seconds+=(end_s - start_s);
                newNumVertices++;
            }
            else
            {
                increment++;
                decrements[j+offset] = increment;
            }
        }
        size_t new_face_num = 0;
        for(int j = 0 ; j<numFaces; j++)
        {
            unsigned int f[3];
            f[0] = modelFaces[j*3] + offset;
            f[1] = modelFaces[j*3+1] + offset;
            f[2] = modelFaces[j*3+2] + offset;

            ofs_faces << "3 ";
            unsigned int newface[3];
            unsigned char a = 3;
//            ofs_faces.write((char*)&a, sizeof(unsigned char));
            for(int k = 0 ; k < 3; k++)
            {
                size_t face_idx = 0;
                if(oldToNew.find(f[k]) == oldToNew.end())
                {
                    auto decr_itr = decrements.upper_bound(f[k]);
                    decr_itr--;
                    face_idx = f[k] - decr_itr->second;
                }
                else
                {
                    auto decr_itr = decrements.upper_bound(oldToNew[f[k]]);
                    decr_itr--;
                    face_idx = oldToNew[f[k]]- decr_itr->second;
//
                }
                start_s = lvr::timestamp.getElapsedTimeInS();
                ofs_faces << face_idx;
                end_s = lvr::timestamp.getElapsedTimeInS();
                seconds+=(end_s - start_s);
                if(k!=2) ofs_faces << " ";

            }
            start_s = lvr::timestamp.getElapsedTimeInS();
            ofs_faces << endl;
            end_s = lvr::timestamp.getElapsedTimeInS();
            seconds+=(end_s - start_s);
//            ofs_faces.write( (char*) newface,sizeof(unsigned int)*3);
            // todo: sort

        }




    }
    ofs_faces.close();
    ofs_vertices.close();
    cout << lvr::timestamp << "saving ply" << endl;
    cout << "Largest decrement: " << increment << endl;
    double start_s = lvr::timestamp.getElapsedTimeInS();

    ofstream ofs_ply("bigMesh.ply", std::ofstream::out | std::ofstream::trunc);
    ifstream ifs_faces("largeFaces.bin");
    ifstream ifs_vertices("largeVertices.bin");
    string line;
    ofs_ply << "ply\n"
            "format ascii 1.0\n"
            "element vertex " << newNumVertices << "\n"
            "property float x\n"
            "property float y\n"
            "property float z\n"
            "element face " << newNumFaces << "\n"
            "property list uchar int vertex_indices\n"
            "end_header" << endl;
//    ofs_ply.close();
//    ofstream ofs_ply_binary("bigMesh.ply",std::ofstream::out | std::ofstream::app | std::ofstream::binary );
//    char a1 = 10;
//    char a2 = 32;
//    char a3 = 180;
//    char a4 = 174;
//    ofs_ply_binary.write(&a1,1);
//    ofs_ply_binary.write(&a2,1);
//    ofs_ply_binary.write(&a3,1);
//    ofs_ply_binary.write(&a4,1);
//    istreambuf_iterator<char> begin_source(ifs_vertices);
//    istreambuf_iterator<char> end_source;
//    ostreambuf_iterator<char> begin_dest(ofs_ply_binary);
//    copy(begin_source, end_source, begin_dest);
//    istreambuf_iterator<char> begin_source2(ifs_faces);
//    istreambuf_iterator<char> end_source2;
//    ostreambuf_iterator<char> begin_dest2(ofs_ply_binary);
//    copy(begin_source2, end_source2, begin_dest2);
    while(std::getline(ifs_vertices,line))
    {
        ofs_ply << line << endl;
    }
    size_t c = 0;
    while(std::getline(ifs_faces,line))
    {

        ofs_ply << line << endl;
        stringstream ss(line);
        unsigned int v[3];
        ss >> v[0];
        ss >> v[1];
        ss >> v[2];
        for(int i = 0 ; i< 3; i++)
        {
            if(v[i]>=newNumVertices)
            {
                cout << "WTF: FACE " << c << " has index " << v[i] << endl;
            }

        }
        c++;
    }
    ofs_ply << endl;
    double end_s = lvr::timestamp.getElapsedTimeInS();
    seconds+=(end_s - start_s);


    cout << "IO-TIME: " << seconds << " seconds" << endl;
    cout << "DATASTRUCT-Time " << datastruct_time << endl;
    cout << "NORMAL-Time " << normal_time << endl;
    cout << "DIST-Time " << dist_time << endl;
    cout << "MESH-Time " << mesh_time << endl;
    cout << "MERGETime " << merge_time << endl;
    cout << "dup_time " << dup_time << endl;
    cout << lvr::timestamp << "finished" << endl;
    cout << "saving largeNormal.ply" << endl;

    if(options.savePointNormals() || options.onlyNormals())
    {
        size_t numNormalPoints = 0;
        bool normalsWithColor = false;
        for(size_t i = 0 ; i <normal_files.size() ; i++)
        {
            string ply_path = normal_files[i];
            LineReader lr(ply_path);
            size_t numPoints = lr.getNumPoints();
            numNormalPoints += numPoints;
            normalsWithColor = lr.getFileType() == XYZNRGB;
        }

        lvr::floatArr normalPoints(new float[numNormalPoints*3]);
        lvr::floatArr normalNormals(new float[numNormalPoints*3]);
        lvr::ucharArr normalColors;
        if(normalsWithColor) normalColors = lvr::ucharArr(new unsigned char[numNormalPoints*3]);

        size_t globalId = 0;
        for(size_t i = 0 ; i <normal_files.size() ; i++)
        {
            string ply_path = normal_files[i];

            auto m = ModelFactory::readModel(ply_path);
            if(m)
            {
                size_t amount;
                auto p = m->m_pointCloud->getPointArray(amount);
                if(p)
                {
                    for(size_t j = 0 ; j < amount*3 ; j++)
                    {
                        normalPoints[globalId+j]=p[j];

                    }
                    size_t normalAmount;
                    auto n = m->m_pointCloud->getPointNormalArray(normalAmount);
                    for(size_t j = 0 ; j < normalAmount*3 ; j++)
                    {
                        normalNormals[globalId+j]=n[j];

                    }
                    size_t colorAmount;
                    auto c = m->m_pointCloud->getPointColorArray(colorAmount);
                    for(size_t j = 0 ; j < colorAmount*3 ; j++)
                    {
                        normalColors[globalId+j]=c[j];

                    }
                    globalId+=amount*3;
                }
            }


        }

        PointBufferPtr normalPB(new PointBuffer);
        normalPB->setPointArray(normalPoints,globalId/3);
        normalPB->setPointNormalArray(normalNormals,globalId/3);
        if(normalsWithColor)
        {
            normalPB->setPointColorArray(normalColors,globalId/3);
        }
        ModelPtr nModel(new Model);
        nModel->m_pointCloud = normalPB;
        ModelFactory::saveModel(nModel,"bigNormals.ply");
    }



    ofs_ply.close();
    ifs_faces.close();
    ifs_vertices.close();
    return 0;
}