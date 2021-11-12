/**
* This file is part of ORB-SLAM2.
* Copyright (C) 2014-2016 Raúl Mur-Artal <raulmur at unizar dot es> (University of Zaragoza)
* For more information see <https://github.com/raulmur/ORB_SLAM2>
* 
* Modification: EAO-SLAM
* Version: 1.0
* Created: 11/23/2019
* Author: Yanmin Wu
* E-mail: wuyanminmax@gmail.com
*/
/**
* This file is part of ORB-SLAM2.
* 通过 深度图 生成点云图 滤波 分割平面后 生产 octomap 更新占有格 概率 保存 在胖果林中显示
*/

#include "MapDrawer.h"
#include "MapPoint.h"
#include "KeyFrame.h"
#include "ProbabilityMapping.h"
#include <pangolin/pangolin.h>
#include <mutex>

#include "Object.h"

namespace ORB_SLAM2
{


MapDrawer::MapDrawer(Map* pMap, const string &strSettingPath):mpMap(pMap)
{
    cv::FileStorage fSettings(strSettingPath, cv::FileStorage::READ);

    mKeyFrameSize = fSettings["Viewer.KeyFrameSize"];
    mKeyFrameLineWidth = fSettings["Viewer.KeyFrameLineWidth"];
    mGraphLineWidth = fSettings["Viewer.GraphLineWidth"];
    mPointSize = fSettings["Viewer.PointSize"];
    mCameraSize = fSettings["Viewer.CameraSize"];
    mCameraLineWidth = fSettings["Viewer.CameraLineWidth"];

    ///////////////////////////////////////////////////////////////

    fx = 558.341390;
    fy = 558.387543;
    cx = 314.763671;
    cy = 240.992295;


    fx = fSettings["Camera.fx"];
    fy = fSettings["Camera.fy"];
    cx = fSettings["Camera.cx"];
    cy = fSettings["Camera.cy"];


    m_octree = new octomap::ColorOcTree(m_res);
    // initialize octomap
    m_octree->setClampingThresMin(0.02);
    m_octree->setClampingThresMax(0.98);
    m_octree->setProbHit(1.7);
    m_octree->setProbMiss(0.1);

    m_treeDepth = m_octree->getTreeDepth();
    m_maxTreeDepth = m_treeDepth;

    boost::filesystem::path octmapPath("octomap.ot");
    if(boost::filesystem::exists(octmapPath))
    {
        bIsLocalization = false;
        LoadOctoMap();

    }
    else
    {
        bIsLocalization = false;
    }
}

// --------- octomap draw and set start  ---------- //
void MapDrawer::LoadOctoMap()
{
    octomap::AbstractOcTree* tree = octomap::AbstractOcTree::read("octomap.ot");
    m_octree= dynamic_cast<octomap::ColorOcTree*> (tree);
}

void MapDrawer::UpdateOctomap(vector<KeyFrame*> vKFs)
{
    int N = vKFs.size();
    for ( size_t i=lastKeyframeSize; i<N ; i++ )
    {
         Eigen::Isometry3d pose = ORB_SLAM2::Converter::toSE3Quat( vKFs[i]->GetPose());

         pcl::PointCloud<pcl::PointXYZRGB>  ground;
         pcl::PointCloud<pcl::PointXYZRGB>  nonground;

         GeneratePointCloud( vKFs[i], ground, nonground);
         octomap::point3d sensorOrigin = octomap::point3d( pose(0,3), pose(1,3), pose(2,3));
         InsertScan(sensorOrigin, ground, nonground);
    }

     lastKeyframeSize = N;
}



void MapDrawer::GeneratePointCloud(KeyFrame *kf, pcl::PointCloud<pcl::PointXYZRGB> &ground, pcl::PointCloud<pcl::PointXYZRGB> &nonground)
{
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud (new pcl::PointCloud<pcl::PointXYZRGB>);
    for ( int m=0; m<(kf->mImDep.rows); m+=1 )
    {
          for ( int n=0; n<(kf->mImDep.cols); n+=1 )
          {
              float d = kf->mImDep.ptr<float>(m)[n];///1000.0;
              if (d < 0.01 || d>10.0)
                 continue;
              pcl::PointXYZRGB p;
              p.z = d;
              p.x = ( n - cx) * p.z / fx;
              p.y = ( m - cy) * p.z / fy;
              if(p.y<-3.0 || p.y>3.0) continue;
              p.b = kf->mImRGB.ptr<uchar>(m)[n*3];
              p.g = kf->mImRGB.ptr<uchar>(m)[n*3+1];
              p.r = kf->mImRGB.ptr<uchar>(m)[n*3+2];
              cloud->points.push_back( p );

          }
    }
    cout << "input cloud size="<<cloud->size()<<endl;
    pcl::VoxelGrid<pcl::PointXYZRGB>vg;
    vg.setInputCloud(cloud);
    vg.setLeafSize(0.01,0.01, 0.01);
    //vg.filter(*cloud);
    cout << "filter cloud size="<<cloud->size()<<endl;
    //STEP2: transform camera to word
    Eigen::Isometry3d T = ORB_SLAM2::Converter::toSE3Quat( kf->GetPose() );
    pcl::PointCloud<pcl::PointXYZRGB> temp;
    pcl::transformPointCloud( *cloud, temp, T.inverse().matrix());

    // filter ground plane
    if(temp.size()<50)
    {
        printf("pointcloud too small skip ground plane extraction\n;");
        ground = temp;
    }
    else
    {
        pcl::ModelCoefficients::Ptr coefficients(new pcl::ModelCoefficients);
        pcl::PointIndices::Ptr inliers(new pcl::PointIndices);

        pcl::SACSegmentation<pcl::PointCloud<pcl::PointXYZRGB>::PointType> seg;
        seg.setOptimizeCoefficients(true);
        seg.setModelType(pcl::SACMODEL_PERPENDICULAR_PLANE);
        seg.setMethodType(pcl::SAC_RANSAC);
        seg.setMaxIterations(200);
        seg.setDistanceThreshold(0.04);
        seg.setAxis(Eigen::Vector3f(0, 1 ,0));
        seg.setEpsAngle(0.5);

        pcl::PointCloud<pcl::PointXYZRGB> cloud_filtered(temp);
        pcl::ExtractIndices<pcl::PointCloud<pcl::PointXYZRGB>::PointType> extract;
        bool groundPlaneFound = false;
        while(cloud_filtered.size()>10 && !groundPlaneFound)
        {
            seg.setInputCloud(cloud_filtered.makeShared());
            seg.segment(*inliers, *coefficients);
            if(inliers->indices.size()==0)
            {
                break;
            }
            extract.setInputCloud(cloud_filtered.makeShared());
            extract.setIndices(inliers);
            if (std::abs(coefficients->values.at(3)) >0.07)
            {
                printf("Ground plane found: %zu/%zu inliers. Coeff: %f %f %f %f", inliers->indices.size(),
                                                                                  cloud_filtered.size(),
                                                                                  coefficients->values.at(0),
                                                                                  coefficients->values.at(1),
                                                                                  coefficients->values.at(2),
                                                                                  coefficients->values.at(3));
                extract.setNegative (false);
                extract.filter (ground);
                // remove ground points from full pointcloud:
                // workaround for PCL bug:
                if(inliers->indices.size() != cloud_filtered.size())
                {
                  extract.setNegative(true);
                  pcl::PointCloud<pcl::PointXYZRGB> cloud_out;
                  extract.filter(cloud_out);
                  nonground += cloud_out;
                  cloud_filtered = cloud_out;
                }

                groundPlaneFound = true;
            }
            else
            {
                printf("Horizontal plane (not ground) found: %zu/%zu inliers. Coeff: %f %f %f %f", inliers->indices.size(),
                                                                                                   cloud_filtered.size(),
                                                                                                   coefficients->values.at(0),
                                                                                                   coefficients->values.at(1),
                                                                                                   coefficients->values.at(2),
                                                                                                   coefficients->values.at(3));
                pcl::PointCloud<pcl::PointXYZRGB> cloud_out;
                extract.setNegative (false);
                extract.filter(cloud_out);
                nonground +=cloud_out;
                if(inliers->indices.size() != cloud_filtered.size())
                {
                     extract.setNegative(true);
                     cloud_out.points.clear();
                     extract.filter(cloud_out);
                     cloud_filtered = cloud_out;
                 }
                else
                {
                     cloud_filtered.points.clear();
                }
            }

         }//while

         if(!groundPlaneFound)
         {
             nonground = temp;
         }
    }
}


void MapDrawer::InsertScan(octomap::point3d sensorOrigin, pcl::PointCloud<pcl::PointXYZRGB> &ground, pcl::PointCloud<pcl::PointXYZRGB> &nonground)
{

    if(!m_octree->coordToKeyChecked(sensorOrigin, m_updateBBXMin)|| !m_octree->coordToKeyChecked(sensorOrigin, m_updateBBXMax))
     {
            printf("coulde not generate key for origin\n");
     }
     octomap::KeySet free_cells, occupied_cells;
     for(auto p:ground.points)
     {
        octomap::point3d point(p.x, p.y, p.z);
        // only clear space (ground points)
        if(m_octree->computeRayKeys(sensorOrigin, point, m_keyRay))
        {
             free_cells.insert(m_keyRay.begin(), m_keyRay.end());
             m_octree->averageNodeColor(p.x, p.y, p.z, p.r,p.g, p.b);
        }
        octomap::OcTreeKey endKey;
        if(m_octree->coordToKeyChecked(point, endKey))
        {
              updateMinKey(endKey, m_updateBBXMin);
              updateMaxKey(endKey, m_updateBBXMax);
         }
        else
        {
              printf("could not generator key for endpoint");
        }
     }

      // all other points : free on ray, occupied on endpoings:
     for(auto p:nonground.points)
     {
         octomap::point3d point(p.x, p.y, p.z);
         //free cell
         if(m_octree->computeRayKeys(sensorOrigin, point, m_keyRay))
         {
            // free_cells.insert(m_keyRay.begin(),m_keyRay.end());
         }
         //occupided endpoint
         octomap::OcTreeKey key;
         if(m_octree->coordToKeyChecked(point, key))
         {
             occupied_cells.insert(key);
             updateMinKey(key, m_updateBBXMin);
             updateMaxKey(key, m_updateBBXMax);
             m_octree->averageNodeColor(p.x, p.y, p.z, p.r,p.g, p.b);
         }

     }pcl::PointCloud<pcl::PointXYZRGB>observation;
     for(octomap::KeySet::iterator it = free_cells.begin(), end= free_cells.end(); it!=end; ++it)
     {
         if(occupied_cells.find(*it) == occupied_cells.end())
         {
             m_octree->updateNode(*it, false);
         }
     }

     for(octomap::KeySet::iterator it = occupied_cells.begin(), end= occupied_cells.end(); it!=end; ++it)
     {
         m_octree->updateNode(*it, true);
     }

     m_octree->prune();
}


void MapDrawer::heightMapColor(double h, double& r, double &g, double& b)
{

    double s = 1.0;
    double v = 1.0;

    h -= floor(h);
    h *= 6;

    int i;
    double m, n, f;

    i = floor(h);
    f = h - i;

    if(!(i & 1))
    {
        f = 1 - f;
    }
    m = v * (1-s);
    n = v * (1- s*f);

    switch(i)
    {
        case 6:
        case 0:
            r = v; g = n; b = m;
            break;
        case 1:
            r = n; g = v; b = m;
            break;
        case 2:
            r = m; g = v; b = n;
            break;
        case 3:
            r = m; g = n; b = v;
            break;
        case 4:
            r = n; g = m; b = v;
            break;
        case 5:
            r = v; g = m; b = n;
            break;
        default:
            r = 1; g = 0.5; b = 0.5;
         break;

    }

}


void MapDrawer::SaveOctoMap(const char *filename)
{
    std::ofstream outfile(filename, std::ios_base::out | std::ios_base::binary);
    if (outfile.is_open())
    {
        m_octree->write(outfile);
        outfile.close();
    }
}

void MapDrawer::DrawOctoMap()
{
    vector<KeyFrame*> vKFs= mpMap->GetAllKeyFrames();
    int N = vKFs.size();

    if(N==0)
    {
        m_octree->clear();
        lastKeyframeSize = 0;
        return;
    }
    if(bIsLocalization == false)
    UpdateOctomap(vKFs);



    octomap::ColorOcTree::tree_iterator it  = m_octree->begin_tree();
    octomap::ColorOcTree::tree_iterator end = m_octree->end_tree();
    int counter = 0;
    double occ_thresh = 0.9;
    int level = 16;
    glClearColor(1.0f,1.0f,1.0f,1.0f);

    glDisable(GL_LIGHTING);
    glEnable (GL_BLEND);
    ////DRAW OCTOMAP BEGIN//////
    double stretch_factor = 128/(1 - occ_thresh); //occupancy range in which the displayed cubes can be
    for(; it != end; ++counter, ++it)
    {
        if(level != it.getDepth())
        {
            continue;
        }
        double occ = it->getOccupancy();
        if(occ < occ_thresh)
        {
            continue;
        }

        double minX, minY, minZ, maxX, maxY, maxZ;
        m_octree->getMetricMin(minX, minY, minZ);
        m_octree->getMetricMax(maxX, maxY, maxZ);

       float halfsize = it.getSize()/2.0;
       float x = it.getX();
       float y = it.getY();
       float z = it.getZ();
       double h = ( std::min(std::max((y-minY)/(maxY-minY), 0.0), 1.0))*0.8;
       double r, g, b;
       heightMapColor(h, r,g,b);
       glBegin(GL_TRIANGLES);
       //Front
       glColor3d(r, g, b);
       glVertex3f(x-halfsize,y-halfsize,z-halfsize);// - - - 1
       glVertex3f(x-halfsize,y+halfsize,z-halfsize);// - + - 2
       glVertex3f(x+halfsize,y+halfsize,z-halfsize);// + + -3

       glVertex3f(x-halfsize,y-halfsize,z-halfsize); // - - -
       glVertex3f(x+halfsize,y+halfsize,z-halfsize); // + + -
       glVertex3f(x+halfsize,y-halfsize,z-halfsize); // + - -4

       //Back
       glVertex3f(x-halfsize,y-halfsize,z+halfsize); // - - + 1
       glVertex3f(x+halfsize,y-halfsize,z+halfsize); // + - + 2
       glVertex3f(x+halfsize,y+halfsize,z+halfsize); // + + + 3

       glVertex3f(x-halfsize,y-halfsize,z+halfsize); // - - +
       glVertex3f(x+halfsize,y+halfsize,z+halfsize); // + + +
       glVertex3f(x-halfsize,y+halfsize,z+halfsize); // - + + 4

       //Left
       glVertex3f(x-halfsize,y-halfsize,z-halfsize); // - - - 1
       glVertex3f(x-halfsize,y-halfsize,z+halfsize); // - - + 2
       glVertex3f(x-halfsize,y+halfsize,z+halfsize); // - + + 3

       glVertex3f(x-halfsize,y-halfsize,z-halfsize); // - - -
       glVertex3f(x-halfsize,y+halfsize,z+halfsize); // - + +
       glVertex3f(x-halfsize,y+halfsize,z-halfsize); // - + - 4

       //Right
       glVertex3f(x+halfsize,y-halfsize,z-halfsize);
       glVertex3f(x+halfsize,y+halfsize,z-halfsize);
       glVertex3f(x+halfsize,y+halfsize,z+halfsize);

       glVertex3f(x+halfsize,y-halfsize,z-halfsize);
       glVertex3f(x+halfsize,y+halfsize,z+halfsize);
       glVertex3f(x+halfsize,y-halfsize,z+halfsize);

       //top
       glVertex3f(x-halfsize,y-halfsize,z-halfsize);
       glVertex3f(x+halfsize,y-halfsize,z-halfsize);
       glVertex3f(x+halfsize,y-halfsize,z+halfsize);

       glVertex3f(x-halfsize,y-halfsize,z-halfsize);
       glVertex3f(x+halfsize,y-halfsize,z+halfsize);
       glVertex3f(x-halfsize,y-halfsize,z+halfsize);

       //bottom
       glVertex3f(x-halfsize,y+halfsize,z-halfsize);
       glVertex3f(x-halfsize,y+halfsize,z+halfsize);
       glVertex3f(x+halfsize,y+halfsize,z+halfsize);

       glVertex3f(x-halfsize,y+halfsize,z-halfsize);
       glVertex3f(x+halfsize,y+halfsize,z+halfsize);
       glVertex3f(x+halfsize,y+halfsize,z-halfsize);
       glEnd();

       glBegin(GL_LINES);
       glColor3f(0,0,0);
       //
       glVertex3f(x-halfsize,y-halfsize,z-halfsize);// - - - 1
       glVertex3f(x-halfsize,y+halfsize,z-halfsize);

       glVertex3f(x-halfsize,y+halfsize,z-halfsize);// - + - 2
       glVertex3f(x+halfsize,y+halfsize,z-halfsize);// + + -3

       glVertex3f(x+halfsize,y+halfsize,z-halfsize);// + + -3
       glVertex3f(x+halfsize,y-halfsize,z-halfsize); // + - -4

       glVertex3f(x+halfsize,y-halfsize,z-halfsize); // + - -4
       glVertex3f(x-halfsize,y-halfsize,z-halfsize);// - - - 1


       // back

       glVertex3f(x-halfsize,y-halfsize,z+halfsize); // - - + 1
       glVertex3f(x+halfsize,y-halfsize,z+halfsize); // + - + 2

       glVertex3f(x+halfsize,y-halfsize,z+halfsize); // + - + 2
       glVertex3f(x+halfsize,y+halfsize,z+halfsize); // + + + 3

       glVertex3f(x+halfsize,y+halfsize,z+halfsize); // + + + 3
       glVertex3f(x-halfsize,y+halfsize,z+halfsize); // - + + 4

       glVertex3f(x-halfsize,y+halfsize,z+halfsize); // - + + 4
       glVertex3f(x-halfsize,y-halfsize,z+halfsize); // - - + 1

       // top
       glVertex3f(x+halfsize,y-halfsize,z-halfsize);
       glVertex3f(x+halfsize,y-halfsize,z+halfsize);

       glVertex3f(x-halfsize,y-halfsize,z+halfsize);
       glVertex3f(x-halfsize,y-halfsize,z-halfsize);

        // bottom

       glVertex3f(x-halfsize,y+halfsize,z+halfsize);
       glVertex3f(x+halfsize,y+halfsize,z+halfsize);

       glVertex3f(x-halfsize,y+halfsize,z-halfsize);
       glVertex3f(x+halfsize,y+halfsize,z-halfsize);
       glEnd();
    }
}


void MapDrawer::DrawGrid()
{
    glBegin(GL_LINES);
    glLineWidth(1);

    glColor3f(0.5,0.5,0.5); //gray
    int size =10;
    for(int i = -size; i <= size ; i++){

      glVertex3f(i,0.6,  size);
      glVertex3f(i, 0.6, -size);
      glVertex3f( size, 0.6, i);
      glVertex3f(-size, 0.6, i);
    }

    glEnd();
}
void MapDrawer::DrawObs(pcl::PointCloud<pcl::PointXYZRGB> observation)
{
    glPointSize(mPointSize);
    glBegin(GL_POINTS);
    glColor3f(1.0,0.0,0.0);

    for(int i=0; i<observation.points.size(); i++)
    {


        glVertex3f(observation.points[i].x,observation.points[i].y,observation.points[i].z);

    }

    glEnd();
}

// --------- octomap draw and set finish  ---------- //


// --------- mappoint draw and set  ---------- //
void MapDrawer::DrawMapPoints()
{
    const vector<MapPoint*> &vpMPs = mpMap->GetAllMapPoints();
    const vector<MapPoint*> &vpRefMPs = mpMap->GetReferenceMapPoints();

    set<MapPoint*> spRefMPs(vpRefMPs.begin(), vpRefMPs.end());

    if(vpMPs.empty())
        return;

    glPointSize(mPointSize);
    glBegin(GL_POINTS);
    glColor3f(0.0,0.0,0.0);
    //glColor3f(1.0,1.0,1.0);

    for(size_t i=0, iend=vpMPs.size(); i<iend;i++)
    {
        if(vpMPs[i]->isBad() || spRefMPs.count(vpMPs[i]))
            continue;
        cv::Mat pos = vpMPs[i]->GetWorldPos();
        glVertex3f(pos.at<float>(0),pos.at<float>(1),pos.at<float>(2));
    }
    glEnd();

    glPointSize(mPointSize);
    glBegin(GL_POINTS);
    glColor3f(1.0,0.0,0.0);
    //glColor3f(0.5,0.5,0.5);

    for(set<MapPoint*>::iterator sit=spRefMPs.begin(), send=spRefMPs.end(); sit!=send; sit++)
    {
        if((*sit)->isBad())
            continue;
        cv::Mat pos = (*sit)->GetWorldPos();
        glVertex3f(pos.at<float>(0),pos.at<float>(1),pos.at<float>(2));

    }
    glEnd();
}


// --------- semidense line draw and set  ---------- //
void MapDrawer::DrawSemiDense(const double sigma)
{
    const vector<KeyFrame*> &vpKf = mpMap->GetAllKeyFrames();
    if(vpKf.empty())return;

    glPointSize(mPointSize);
    glBegin(GL_POINTS);
    glColor3f(0.0,1.0,0.0);

    int draw_cnt(0);
    for(size_t i = 0; i < vpKf.size();++i)
    {
        KeyFrame* kf = vpKf[i];
        kf->SetNotEraseDrawer();
        if( kf->isBad() || !kf->semidense_flag_ || !kf->interKF_depth_flag_) {
            kf->SetEraseDrawer();
            continue;
        }

        unique_lock<mutex> lock(kf->mMutexSemiDensePoints);

        draw_cnt ++;
        for(int y = 0; y< kf->im_.rows; y++)
            for(int x = 0; x< kf->im_.cols; x++)
            {
                if (kf->depth_sigma_.at<float>(y,x) > sigma) continue;

                if( kf->depth_map_checked_.at<float>(y,x) > 0.000001 )
                {
                    Eigen::Vector3f Pw  (kf->SemiDensePointSets_.at<float>(y,3*x),
                                         kf->SemiDensePointSets_.at<float>(y,3*x+1),
                                         kf->SemiDensePointSets_.at<float>(y,3*x+2));

                    float b = kf->rgb_.at<uchar>(y, 3*x) / 255.0;
                    float g = kf->rgb_.at<uchar>(y, 3*x+1) / 255.0;
                    float r = kf->rgb_.at<uchar>(y, 3*x+2) / 255.0;
                    glColor3f(r, g, b);

                    glVertex3f( Pw[0],Pw[1],Pw[2]);
                }
            }
        kf->SetEraseDrawer();
    }
    glEnd();
}

void MapDrawer::DrawModel()
{
    const vector<KeyFrame*> &vpKf = mpMap->GetAllKeyFrames();
    Model* pModel = mpMap->GetModel();
    if(vpKf.empty()) return;
    if(pModel == NULL) return;

    pModel->SetNotErase();

    // get the most recent reconstructed keyframe to texture
    KeyFrame* kfToTexture = NULL;
    KeyFrame* prevKf = NULL;
    for(size_t i = 0; i < vpKf.size();++i) {
        KeyFrame *kf = vpKf[i];
        kf->SetNotEraseDrawer();
        if (kf->isBad()) {
            kf->SetEraseDrawer();
            continue;
        }
        if (prevKf == NULL){
            kfToTexture = kf;
            prevKf = kf;
        } else if (kf->mnId > prevKf->mnId){
            kfToTexture = kf;
            prevKf->SetEraseDrawer();
            prevKf = kf;
        }
    }
    if (kfToTexture == NULL) return;


    static unsigned int frameTex = 0;
    if (!frameTex)
        glGenTextures(1, &frameTex);

    cv::Size imSize = kfToTexture->rgb_.size();

    glBindTexture(GL_TEXTURE_2D, frameTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    // image are saved in RGB format, grayscale images are converted
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
                 imSize.width, imSize.height, 0,
                 GL_BGR,
                 GL_UNSIGNED_BYTE,
                 kfToTexture->rgb_.data);


    glEnable(GL_TEXTURE_2D);

    glBegin(GL_TRIANGLES);
    glColor3f(1.0,1.0,1.0);

    for (list<dlovi::Matrix>::const_iterator it = pModel->GetTris().begin(); it != pModel->GetTris().end(); it++) {

        dlovi::Matrix point0 = pModel->GetPoints()[(*it)(0)];
        dlovi::Matrix point1 = pModel->GetPoints()[(*it)(1)];
        dlovi::Matrix point2 = pModel->GetPoints()[(*it)(2)];

        vector<float> uv0 = kfToTexture->GetTexCoordinate(point0(0),point0(1),point0(2));
        vector<float> uv1 = kfToTexture->GetTexCoordinate(point1(0),point1(1),point1(2));
        vector<float> uv2 = kfToTexture->GetTexCoordinate(point2(0),point2(1),point2(2));

        // if all vertices are projected in the image
        if (uv0.size() == 2 && uv1.size() == 2 && uv2.size() == 2) {

            glTexCoord2f(uv0[0], uv0[1]);
            glVertex3d(point0(0), point0(1), point0(2));

            glTexCoord2f(uv1[0], uv1[1]);
            glVertex3d(point1(0), point1(1), point1(2));

            glTexCoord2f(uv2[0], uv2[1]);
            glVertex3d(point2(0), point2(1), point2(2));

        }
    }

    glEnd();

    glDisable(GL_TEXTURE_2D);


    kfToTexture->SetEraseDrawer();

    pModel->SetErase();

}

void MapDrawer::DrawTriangles(pangolin::OpenGlMatrix &Twc)
{
    Model* pModel = mpMap->GetModel();
    if(pModel == NULL) return;

    pModel->SetNotErase();


    glPushMatrix();

#ifdef HAVE_GLES
    glMultMatrixf(Twc.m);
#else
    glMultMatrixd(Twc.m);
#endif

    GLfloat light_position[] = { 0.0, 0.0, 1.0, 0.0 };
    glLightfv(GL_LIGHT0, GL_POSITION, light_position);

    glPopMatrix();

    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);

    glShadeModel(GL_FLAT);

    GLfloat material_diffuse[] = {0.2, 0.5, 0.8, 1};
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, material_diffuse);

    glBegin(GL_TRIANGLES);
    glColor3f(1.0,1.0,1.0);

    for (list<dlovi::Matrix>::const_iterator it = pModel->GetTris().begin(); it != pModel->GetTris().end(); it++) {

        dlovi::Matrix point0 = pModel->GetPoints()[(*it)(0)];
        dlovi::Matrix point1 = pModel->GetPoints()[(*it)(1)];
        dlovi::Matrix point2 = pModel->GetPoints()[(*it)(2)];

        dlovi::Matrix edge10 = point1 - point0;
        dlovi::Matrix edge20 = point2 - point0;

        dlovi::Matrix normal = edge20.cross(edge10);
        normal = normal / normal.norm();

        glNormal3d(normal(0), normal(1), normal(2));

        glVertex3d(point0(0), point0(1), point0(2));
        glVertex3d(point1(0), point1(1), point1(2));
        glVertex3d(point2(0), point2(1), point2(2));

    }
    glEnd();

    glDisable(GL_LIGHTING);

    pModel->SetErase();

}

void MapDrawer::DrawFrame()
{
    const vector<KeyFrame*> &vpKf = mpMap->GetAllKeyFrames();
    if(vpKf.empty()) return;

    // get the most recent reconstructed keyframe to texture
    KeyFrame* kfToTexture = NULL;
    KeyFrame* prevKf = NULL;
    for(size_t i = 0; i < vpKf.size();++i) {
        KeyFrame *kf = vpKf[i];
        kf->SetNotEraseDrawer();
        if (kf->isBad() || !kf->semidense_flag_ || !kf->interKF_depth_flag_) {
            kf->SetEraseDrawer();
            continue;
        }
        if (prevKf == NULL){
            kfToTexture = kf;
            prevKf = kf;
        } else if (kf->mnId > prevKf->mnId){
            kfToTexture = kf;
            prevKf->SetEraseDrawer();
            prevKf = kf;
        }
    }
    if (kfToTexture == NULL) return;


    cv::Size imSize = kfToTexture->rgb_.size();

    pangolin::GlTexture imageTexture(imSize.width, imSize.height, GL_RGB, false, 0, GL_BGR,
                                     GL_UNSIGNED_BYTE);

    imageTexture.Upload(kfToTexture->rgb_.data, GL_BGR, GL_UNSIGNED_BYTE);

    imageTexture.RenderToViewportFlipY();


    kfToTexture->SetEraseDrawer();
}



void MapDrawer::DrawKeyFrames(const bool bDrawKF, const bool bDrawGraph)
{
    const float &w = mKeyFrameSize;
    const float h = w*0.75;
    const float z = w*0.6;

    const vector<KeyFrame*> vpKFs = mpMap->GetAllKeyFrames();

    if(bDrawKF)
    {
        for(size_t i=0; i<vpKFs.size(); i++)
        {
            KeyFrame* pKF = vpKFs[i];
            cv::Mat Twc = pKF->GetPoseInverse().t();

            glPushMatrix();

            glMultMatrixf(Twc.ptr<GLfloat>(0));

            glLineWidth(mKeyFrameLineWidth);
            
            // [EAO] created by objects.
            if(pKF->mbCreatedByObjs)
                glColor3f(1.0f,0.0f,0.0f);
            else
                glColor3f(0.0f,0.0f,1.0f);

            glBegin(GL_LINES);
            glVertex3f(0,0,0);
            glVertex3f(w,h,z);
            glVertex3f(0,0,0);
            glVertex3f(w,-h,z);
            glVertex3f(0,0,0);
            glVertex3f(-w,-h,z);
            glVertex3f(0,0,0);
            glVertex3f(-w,h,z);

            glVertex3f(w,h,z);
            glVertex3f(w,-h,z);

            glVertex3f(-w,h,z);
            glVertex3f(-w,-h,z);

            glVertex3f(-w,h,z);
            glVertex3f(w,h,z);

            glVertex3f(-w,-h,z);
            glVertex3f(w,-h,z);
            glEnd();

            glPopMatrix();
        }
    }

    if(bDrawGraph)
    {
        glLineWidth(mGraphLineWidth);
        glColor4f(0.0f,1.0f,0.0f,0.6f);
        glBegin(GL_LINES);

        for(size_t i=0; i<vpKFs.size(); i++)
        {
            // Covisibility Graph
            const vector<KeyFrame*> vCovKFs = vpKFs[i]->GetCovisiblesByWeight(100);
            cv::Mat Ow = vpKFs[i]->GetCameraCenter();
            if(!vCovKFs.empty())
            {
                for(vector<KeyFrame*>::const_iterator vit=vCovKFs.begin(), vend=vCovKFs.end(); vit!=vend; vit++)
                {
                    if((*vit)->mnId<vpKFs[i]->mnId)
                        continue;
                    cv::Mat Ow2 = (*vit)->GetCameraCenter();
                    glVertex3f(Ow.at<float>(0),Ow.at<float>(1),Ow.at<float>(2));
                    glVertex3f(Ow2.at<float>(0),Ow2.at<float>(1),Ow2.at<float>(2));
                }
            }

            // Spanning tree
            KeyFrame* pParent = vpKFs[i]->GetParent();
            if(pParent)
            {
                cv::Mat Owp = pParent->GetCameraCenter();
                glVertex3f(Ow.at<float>(0),Ow.at<float>(1),Ow.at<float>(2));
                glVertex3f(Owp.at<float>(0),Owp.at<float>(1),Owp.at<float>(2));
            }

            // Loops
            set<KeyFrame*> sLoopKFs = vpKFs[i]->GetLoopEdges();
            for(set<KeyFrame*>::iterator sit=sLoopKFs.begin(), send=sLoopKFs.end(); sit!=send; sit++)
            {
                if((*sit)->mnId<vpKFs[i]->mnId)
                    continue;
                cv::Mat Owl = (*sit)->GetCameraCenter();
                glVertex3f(Ow.at<float>(0),Ow.at<float>(1),Ow.at<float>(2));
                glVertex3f(Owl.at<float>(0),Owl.at<float>(1),Owl.at<float>(2));
            }
        }

        glEnd();
    }
}

// BRIEF [EAO-SLAM] draw objects.
void MapDrawer::DrawObject(const bool bCubeObj, const bool QuadricObj, 
                           const string &flag,
                           const bool bShowBottle,  const bool bShowChair, const bool bShowTvmonitors,
                           const bool bShowKeyboard,const bool bShowMouse, const bool bShowBook,   const bool bShowBear)
{
    const vector<Object_Map*> &vObjs = mpMap->GetObjects();

    vector<cv::Mat> object_cen;

    int id = -1;
    for(size_t i = 0; i < vObjs.size(); ++i)
    {
        Object_Map* Obj = vObjs[i];
        
        if((Obj->mObjectFrame.size() < 5) && (flag != "NA"))
            continue;

        if((Obj->mvpMapObjectMappoints.size() < 10) || (Obj->bBadErase == true))
        {
            continue;
        }

        id ++;
        
        // Display a certain category of object.
        // cup.
        if(!bShowBottle && ((Obj->mnClass == 39) || (Obj->mnClass == 41) || (Obj->mnClass == 44) || (Obj->mnClass == 76)))
        {
            continue;
        }
        // chair.
        if(!bShowChair && ((Obj->mnClass == 56) || (Obj->mnClass == 69) || (Obj->mnClass == 72) || (Obj->mnClass == 75)))
        {
            continue;
        }
        // tvmonitor.
        if(!bShowTvmonitors && (Obj->mnClass == 62))
        {
            continue;
        }
        // keyboard.
        if(!bShowKeyboard && (Obj->mnClass == 66))
        {
            continue;
        }
        // mouse.
        if(!bShowMouse && ((Obj->mnClass == 64) || (Obj->mnClass == 65) || (Obj->mnClass == 67)))
        {
            continue;
        }
        // book.
        if(!bShowBook && (Obj->mnClass == 73))
        {
            continue;
        }
        // bear.
        if(!bShowBear && (Obj->mnClass == 77))
        {
            continue;
        }

        // color.
        if(i % 10 == 0)
            glColor3f(1.0,0.0,0.0);
        else if(i % 10 == 1)
            glColor3f(0.0,1.0,0.0);
        else if(i % 10 == 2)
            glColor3f(0.0,0.0,1.0);
        else if(i % 10 == 3)
            glColor3f(0.5,0.0,0.0);
        else if(i % 10 == 4)
            glColor3f(0.0,0.5,0.0);
        else if(i % 10 == 5)
            glColor3f(0.0,0.0,0.5);
        else if(i % 10 == 6)
            glColor3f(1.0,1.0,0);
        else if(i % 10 == 7)
            glColor3f(1.0,0,1.0);
        else if(i % 10 == 8)
            glColor3f(0.5,0.5,0.0);
        else if(i % 10 == 9)
            glColor3f(0.5,0,0.5);

        glLineWidth(mCameraLineWidth);

        // *************************************
        //    STEP 1. [EAO-SLAM] Draw cubes.   *
        // *************************************
        if(bCubeObj && ((Obj->mnClass == 73) || (Obj->mnClass == 64) || (Obj->mnClass == 65) 
                || (Obj->mnClass == 66) || (Obj->mnClass == 56) || (Obj->mnClass == 72)))
        {
            bool bObjAsOrigin = true;

            // object center.
            if(bObjAsOrigin)
            {
                cv::Mat Twobj_t = Converter::toCvMat(Obj->mCuboid3D.pose).t();
                glPushMatrix();
                glMultMatrixf(Twobj_t.ptr<GLfloat>(0));
            }

            // draw object center.
            glPointSize(4*mPointSize);
            glBegin(GL_POINTS);
            if(bObjAsOrigin)
                glVertex3f(0, 0, 0);
            else
                glVertex3f(Obj->mCenter3D.at<float>(0), Obj->mCenter3D.at<float>(1), Obj->mCenter3D.at<float>(2));
            glEnd();

            // ******************************************
            //                 7------6                 *
            //                /|     /|                 *
            //               / |    / |                 *
            //              4------5  |                 *
            //              |  3---|--2                 *
            //              | /    | /                  *
            //              0------1                    *
            // ******************************************

            glBegin(GL_LINES);
            if(bObjAsOrigin)
            {
                float lenth = Obj->mCuboid3D.lenth/2;
                float width = Obj->mCuboid3D.width/2;
                float height = Obj->mCuboid3D.height/2;

                // ? no use
                if(Obj->mnClass == 0)
                {
                    glVertex3f(-lenth, -width, 0);      // 5
                    glVertex3f(lenth, -width, 0);       // 6

                    glVertex3f(lenth, -width, 0);       // 6
                    glVertex3f(lenth, width, 0);        // 7

                    glVertex3f(lenth, width, 0);        // 7
                    glVertex3f(-lenth, width, 0);       // 8

                    glVertex3f(-lenth, width, 0);       // 8
                    glVertex3f(-lenth, -width, 0);      // 5


                    glVertex3f(-lenth, -width, -height);    // 1
                    glVertex3f(-lenth, -width, 0);          // 5

                    glVertex3f(lenth, -width, -height);     // 2
                    glVertex3f(lenth, -width, 0);           // 6

                    glVertex3f(lenth, width, height);       // 9
                    glVertex3f(-lenth, width, height);      // 10

                    glVertex3f(lenth, width, -height);      // 3
                    glVertex3f(lenth, width, height);       // 9

                    glVertex3f(-lenth, width, -height);     // 4
                    glVertex3f(-lenth, width, height);      // 10
                }
                else
                {
                    // chair, fixed scale, for better visulazation.
                    if(Obj->mnClass == 56)
                    {
                        lenth = 0.09;
                        width = 0.08;
                        height = 0.12;
                    }

                    glVertex3f(-lenth, -width, -height);    // 1
                    glVertex3f(lenth, -width, -height);     // 2

                    glVertex3f(lenth, -width, -height);     // 2
                    glVertex3f(lenth, width, -height);      // 3

                    glVertex3f(lenth, width, -height);      // 3
                    glVertex3f(-lenth, width, -height);     // 4

                    glVertex3f(-lenth, width, -height);     // 4
                    glVertex3f(-lenth, -width, -height);    // 1

                    glVertex3f(-lenth, -width, height);     // 5
                    glVertex3f(lenth, -width, height);      // 6

                    glVertex3f(lenth, -width, height);      // 6
                    glVertex3f(lenth, width, height);       // 7

                    glVertex3f(lenth, width, height);       // 7
                    glVertex3f(-lenth, width, height);      // 8

                    glVertex3f(-lenth, width, height);      // 8
                    glVertex3f(-lenth, -width, height);     // 5

                    glVertex3f(-lenth, -width, -height);    // 1
                    glVertex3f(-lenth, -width, height);     // 5

                    glVertex3f(lenth, -width, -height);     // 2
                    glVertex3f(lenth, -width, height);      // 6

                    glVertex3f(lenth, width, -height);      // 3
                    glVertex3f(lenth, width, height);       // 7

                    glVertex3f(-lenth, width, -height);     // 4
                    glVertex3f(-lenth, width, height);      // 8
                }
                glEnd();
                glPopMatrix();
            }
        } // draw cubes END ----------------------------------------------------------------------------

        // ****************************************
        //    STEP 2. [EAO-SLAM] Draw quadrics.   *
        // ****************************************
        if(QuadricObj && !((Obj->mnClass == 73) || (Obj->mnClass == 64) || (Obj->mnClass == 65)
                || (Obj->mnClass == 66) || (Obj->mnClass == 56) || (Obj->mnClass == 72)))
        {
            // half axial length.
            float lenth = Obj->mCuboid3D.lenth/2;
            float width = Obj->mCuboid3D.width/2;
            float height = Obj->mCuboid3D.height/2;

            // tvmonitor, fixed scale, for better visulazation.
            if(Obj->mnClass == 62)
            {
                lenth = 0.13;
                width = 0.035;
                height = 0.08;
            }
            // if(Obj->mnClass == 75)
            // {
            //     lenth = 0.08;
            //     width = 0.08;
            //     height = 0.08;
            // }

            cv::Mat axe = cv::Mat::zeros(3,1,CV_32F);
            axe.at<float>(0) = lenth;
            axe.at<float>(1) = width;
            axe.at<float>(2) = height;

            // quadrcis pose.
            cv::Mat Twq = cv::Mat::zeros(4,4,CV_32F);
            Twq.at<float>(0, 0) = 1;
            Twq.at<float>(0, 1) = 0;
            Twq.at<float>(0, 2) = 0;
            //Twq.at<float>(0, 3) = Obj->mCenter3D.at<float>(0);
            Twq.at<float>(0, 3) = Obj->mCuboid3D.cuboidCenter[0];
            Twq.at<float>(1, 0) = 0;
            Twq.at<float>(1, 1) = 1;
            Twq.at<float>(1, 2) = 0;
            //Twq.at<float>(1, 3) = Obj->mCenter3D.at<float>(1);
            Twq.at<float>(1, 3) = Obj->mCuboid3D.cuboidCenter[1];
            Twq.at<float>(2, 0) = 0;
            Twq.at<float>(2, 1) = 0;
            Twq.at<float>(2, 2) = 1;
            //Twq.at<float>(2, 3) = Obj->mCenter3D.at<float>(2);
            Twq.at<float>(2, 3) = Obj->mCuboid3D.cuboidCenter[2];
            Twq.at<float>(3, 0) = 0;
            Twq.at<float>(3, 1) = 0;
            Twq.at<float>(3, 2) = 0;
            Twq.at<float>(3, 3) = 1;

            // create a quadric.
            GLUquadricObj *pObj = gluNewQuadric();
            cv::Mat Twq_t = Twq.t();

            // color
            cv::Scalar sc;
            sc = cv::Scalar(0, 255, 0);

            // add to display list
            glPushMatrix();
            glMultMatrixf(Twq_t.ptr<GLfloat >(0));
            glScalef(
                    (GLfloat)(axe.at<float>(0,0)),
                    (GLfloat)(axe.at<float>(0,1)),
                    (GLfloat)(axe.at<float>(0,2))
                    );

            gluQuadricDrawStyle(pObj, GLU_LINE);
            gluQuadricNormals(pObj, GLU_NONE);
            glBegin(GL_COMPILE);
            gluSphere(pObj, 1., 15, 10);

            glEnd();
            glPopMatrix();
        // draw quadrics END ---------------------------------------------------------------------
        }

    }
} // draw objects END ----------------------------------------------------------------------------


void MapDrawer::DrawCurrentCamera(pangolin::OpenGlMatrix &Twc)
{
    const float &w = mCameraSize;
    const float h = w*0.75;
    const float z = w*0.6;

    glPushMatrix();

#ifdef HAVE_GLES
        glMultMatrixf(Twc.m);
#else
        glMultMatrixd(Twc.m);
#endif

    glLineWidth(mCameraLineWidth);
    glColor3f(0.0f,1.0f,0.0f);
    glBegin(GL_LINES);
    glVertex3f(0,0,0);
    glVertex3f(w,h,z);
    glVertex3f(0,0,0);
    glVertex3f(w,-h,z);
    glVertex3f(0,0,0);
    glVertex3f(-w,-h,z);
    glVertex3f(0,0,0);
    glVertex3f(-w,h,z);

    glVertex3f(w,h,z);
    glVertex3f(w,-h,z);

    glVertex3f(-w,h,z);
    glVertex3f(-w,-h,z);

    glVertex3f(-w,h,z);
    glVertex3f(w,h,z);

    glVertex3f(-w,-h,z);
    glVertex3f(w,-h,z);
    glEnd();

    glPopMatrix();
}


void MapDrawer::SetCurrentCameraPose(const cv::Mat &Tcw)
{
    unique_lock<mutex> lock(mMutexCamera);
    mCameraPose = Tcw.clone();
}


bool MapDrawer::GetCurrentCameraPos(cv::Mat &Rcw, cv::Mat  &Ow)
{
    bool  flag = false;
    if(!mCameraPose.empty())
    {
        unique_lock<mutex> lock(mMutexCamera);
        Rcw = mCameraPose.rowRange(0,3).colRange(0,3);
        Ow  = -Rcw.t()*mCameraPose.rowRange(0,3).col(3);
        flag = true;
    }

    return flag;
}


void MapDrawer::GetCurrentOpenGLCameraMatrix(pangolin::OpenGlMatrix &M)
{
    if(!mCameraPose.empty())
    {
        cv::Mat Rwc(3,3,CV_32F);
        cv::Mat twc(3,1,CV_32F);
        {
            unique_lock<mutex> lock(mMutexCamera);
            Rwc = mCameraPose.rowRange(0,3).colRange(0,3).t();
            twc = -Rwc*mCameraPose.rowRange(0,3).col(3);
        }

        M.m[0] = Rwc.at<float>(0,0);
        M.m[1] = Rwc.at<float>(1,0);
        M.m[2] = Rwc.at<float>(2,0);
        M.m[3]  = 0.0;

        M.m[4] = Rwc.at<float>(0,1);
        M.m[5] = Rwc.at<float>(1,1);
        M.m[6] = Rwc.at<float>(2,1);
        M.m[7]  = 0.0;

        M.m[8] = Rwc.at<float>(0,2);
        M.m[9] = Rwc.at<float>(1,2);
        M.m[10] = Rwc.at<float>(2,2);
        M.m[11]  = 0.0;

        M.m[12] = twc.at<float>(0);
        M.m[13] = twc.at<float>(1);
        M.m[14] = twc.at<float>(2);
        M.m[15]  = 1.0;
    }
    else
        M.SetIdentity();
}

} //namespace ORB_SLAM
