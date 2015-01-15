//
//  Planet.cpp
//  PlanetRendering
//
//  Created by Christian on 9/28/14.
//  Copyright (c) 2014 Christian. All rights reserved.
//

#include "Planet.h"
#include "glm/glm.hpp"
#include "glm/gtc/type_ptr.hpp"
#include <iostream>
#include <algorithm>
#include <random>
#include <time.h>
#include "MainGame_SDL.h"
#include <thread>
#include "RandomUtils.h"

//Constructor for planet.  Initializes VBO (experimental) and builds the base icosahedron mesh.
Planet::Planet(glm::vec3 pos, vfloat radius, double mass, vfloat seed, Player& _player, GLManager& _glManager, float terrainRegularity)
: Radius(radius),
time(0),
SEED(seed),
CurrentRenderMode(RenderMode::SOLID),
player(_player),
glManager(_glManager),
closed(false),
CurrentRotationMode(RotationMode::NO_ROTATION),
atmosphere(pos, radius*1.01),
PlanetInfo{
//    glm::vec4(0.0945,0.2011,0.45,1.),
//    glm::vec4(0.0867,0.3336,0.51,1.),
//    glm::vec4(0.0513,0.0768,0.27,1.),
//    glm::vec4(0.1438, 0.22, 0.0814,1.),
    //    glm::vec4(0.8, 0.760784, 0.470588,1.),
    glm::vec4(RandomUtils::Uniform<float>(0,0.9f),RandomUtils::Uniform<float>(0,0.9f),RandomUtils::Uniform<float>(0,0.9f),1.),
    glm::vec4(RandomUtils::Uniform<float>(0,0.9f),RandomUtils::Uniform<float>(0,0.9f),RandomUtils::Uniform<float>(0,0.9f),1.),
    glm::vec4(RandomUtils::Uniform<float>(0,0.9f),RandomUtils::Uniform<float>(0,0.9f),RandomUtils::Uniform<float>(0,0.9f),1.),
    glm::vec4(RandomUtils::Uniform<float>(0,0.9f),RandomUtils::Uniform<float>(0,0.9f),RandomUtils::Uniform<float>(0,0.9f),1.),
    glm::vec4(RandomUtils::Uniform<float>(0,0.9f),RandomUtils::Uniform<float>(0,0.9f),RandomUtils::Uniform<float>(0,0.9f),1.),
    glm::vec4(RandomUtils::Uniform<float>(0,0.9f),RandomUtils::Uniform<float>(0,0.9f),RandomUtils::Uniform<float>(0,0.9f),1.),
    vmat4(),
    0.001f,
    0.5f},
PhysicsObject(static_cast<glm::dvec3>(pos), mass),
TERRAIN_REGULARITY(terrainRegularity),
Angle(0),
AngularVelocity(100.,0.0,0)
//5.972E24)
{
    generateBuffers();
    buildBaseMesh();
    //create a separate thread in which updates occur
    std::thread t(&Planet::Update, this);
    updateThread.swap(t);
    
}

//clear allocated memory and OpenGL objects
Planet::~Planet()
{
    closed = true;
    glDeleteVertexArrays(1, &VAO);
    for (auto it = faces.begin(); it!=faces.end();)
    {
        it = faces.erase(it);
    }
    updateThread.join();
}



//This function accepts a boolean-valued function of displacement.  If the function is true, the function will divide the given face into four subfaces, each of which will be recursively subjected to the same subdivision scheme.  It is important that the input function terminates at a particular level of detail, or the program will crash.
bool Planet::trySubdivide(Face* iterator, const std::function<bool (Player&, const Face&)>& func, Player& player)
{
    //perform horizon culling
    if (iterator->level!=0 && !inHorizon(*iterator)) return false;
    if (iterator->level>MAX_LOD) return false;
    
    if (closed) return false;
    
    if (func(player, *iterator))
    {
        if (!iterator->AllChildrenNull())
            return true;
        
        
        //face vertices
        vvec3 v1 = iterator->vertices[0];
        vvec3 v2 = iterator->vertices[1];
        vvec3 v3 = iterator->vertices[2];
        
        vvec2 p1 = iterator->polarCoords[0];
        vvec2 p2 = iterator->polarCoords[1];
        vvec2 p3 = iterator->polarCoords[2];
        
        vvec3 nv1 = glm::normalize(v1);
        vvec3 nv2 = glm::normalize(v2);
        vvec3 nv3 = glm::normalize(v3);
        
        //lengths of face vertices
        vfloat l1 = glm::length(v1)/Radius;
        vfloat l2 = glm::length(v2)/Radius;
        vfloat l3 = glm::length(v3)/Radius;
        
        //normalized midpoints of face vertices
        vvec3 m12 = glm::normalize((nv1 + nv2) * static_cast<vfloat>(0.5));
        vvec3 m13 = glm::normalize((nv1 + nv3) * static_cast<vfloat>(0.5));
        vvec3 m23 = glm::normalize((nv2 + nv3) * static_cast<vfloat>(0.5));
        //normalized midpoints of face vertices (polar coords)
//        vvec2 p12((v1.x+v2.x) / 2,(v1.y+v2.y) / 2);
//        vvec2 p13((v1.x+v3.x) / 2,(v1.y+v3.y) / 2);
//        vvec2 p23((v2.x+v3.x) / 2,(v2.y+v3.y) / 2);
        vvec2 p12(std::fmod<vfloat>(v1.x+v2.x, M_PI) / 2,std::fmod<vfloat>(v1.y+v2.y, M_2_PI) / 2);
        vvec2 p13(std::fmod<vfloat>(v1.x+v3.x, M_PI) / 2,std::fmod<vfloat>(v1.y+v3.y, M_2_PI) / 2);
        vvec2 p23(std::fmod<vfloat>(v2.x+v3.x, M_PI) / 2,std::fmod<vfloat>(v2.y+v3.y, M_2_PI) / 2);
        //height scale of terrain
        //proportional to 2^(-LOD) * nonlinear factor
        //the nonlinear factor is LOD^(TERRAIN_REGULARITY)
        //if the nonlinear factor is 1, the terrain is boring -- this is introduced to make higher-frequency noise more noticeable.
        vfloat fac =static_cast<vfloat>(3.)/static_cast<vfloat>(1 << iterator->level)*std::pow(static_cast<vfloat>(iterator->level+1), TERRAIN_REGULARITY);
        
        
        m12*=1 + terrainNoise(m12) * fac;
        m13*=1 + terrainNoise(m13) * fac;
        m23*=1 + terrainNoise(m23) * fac;
        
        m12*=(l1 + l2)/static_cast<vfloat>(2.)*Radius;
        m13*=(l1 + l3)/static_cast<vfloat>(2.)*Radius;
        m23*=(l2 + l3)/static_cast<vfloat>(2.)*Radius;
        
//        m12+=Position;
//        m13+=Position;
//        m23+=Position;
        Face *f0,*f1,*f2,*f3;
        
        {
            std::lock_guard<std::mutex> lock(renderMutex);
            f0 = new Face(m13,m12,m23,p13,p12,p23,iterator->level+1);
            f1 = new Face(v3,m13,m23,p3,p13,p23,iterator->level+1);
            f2 = new Face(m23,m12,v2,p23,p12,p2,iterator->level+1);
            f3 = new Face(m13,v1,m12,p13,p1,p12,iterator->level+1);
        }
        iterator->children = {f0, f1, f2, f3};
        
        return true;
    }
    return false;
}
//Similarly to trySubdivide, this function combines four faces into a larger face if a boolean-valued function is statisfied.
bool Planet::tryCombine(Face* iterator, const std::function<bool (Player&, const Face&)>& func, Player& player)
{
    if (closed) return false;
    if (iterator==nullptr) return false;
    if (iterator->AnyChildrenNull()) return false;
    if ((func(player, *iterator) || func(player, *iterator->children[0])|| func(player, *iterator->children[1]) || func(player, *iterator->children[2]) || func(player, *iterator->children[3])) && iterator->level>0)
    {
        combineFace(iterator);
        
        return true;
    }
    return false;
}

void Planet::combineFace(Face* face)
{
    if (closed) return;
    if (face->level==0) return;
    for (Face*& f : face->children)
        if (f!=nullptr)
        {
            combineFace(f);
            delete f;
            f = nullptr;
        }
}
//performed in background, manages terrain generation
void Planet::Update()
{
    
    if (closed) return;
    subdivided = false;
    //iterate through faces and perform necessary generation checks
    for (auto it = faces.begin();it!=faces.end();it++)
    {
        if (recursiveCombine(&(*it), player))
            subdivided=true;
        if (recursiveSubdivide(&(*it), player))
            subdivided=true;
    }
    //update vertices if changes were made
    
    unsigned int indsize, vertsize;
    GetIndicesVerticesSizes(indsize, vertsize);
    if (subdivided || vertsize==0) updateVBO(player);
//    std::cout << "Height above earth surface: " << player.DistFromSurface * EARTH_DIAMETER << " m\n";
    //repeat indefinitely (on separate thread)
    
    Update();
}

void Planet::generateBuffers()
{
    //generate vertex array object -- contains state data for other relevant OpenGL objects
    glGenVertexArrays(1, &VAO);
    glBindVertexArray(VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &IBO);
    
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, IBO);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    //set up vertex attributes.  These contain position and normal data for each vertex.
    //Use preprocessor conditionals to differentiate between two possible precisions
#ifdef VERTEX_DOUBLE
    glVertexAttribLPointer(0, 3, GL_DOUBLE, sizeof(Vertex), (void*)__offsetof(Vertex, x));
    glVertexAttribLPointer(1, 3, GL_DOUBLE, sizeof(Vertex), (void*)__offsetof(Vertex, nx));
#else
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),(void*)__offsetof(Vertex, x));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),(void*)__offsetof(Vertex, nx));
#endif
    //unbind VAO
    glBindVertexArray(0);
}

//Deprecated.  This function is a higher-performance alternative to the currently implemented sorting scheme in updateVBO().
//It is faster (sometimes by a factor of 3), but it produces non-ideal normal discontinuities (mainly due to the recursive implementation of the function).
void Planet::recursiveUpdate(Face& face, unsigned int index1, unsigned int index2, unsigned int index3, Player& player, std::vector<Vertex>& newVertices, std::vector<unsigned int>& newIndices)
{
    if (closed) return;
    //Get player distance from face.  If temporary minimum, set player distance (this process naturally finds the player's minimum distance to the surface).
    {
        std::lock_guard<std::mutex> lock(renderMutex);
        vfloat dist = std::min(std::min(glm::length(GetPlayerDisplacement() - face.vertices[0]),glm::length(GetPlayerDisplacement() - face.vertices[1])),glm::length(GetPlayerDisplacement()- face.vertices[2]));
        if (player.DistFromSurface > dist) player.DistFromSurface = dist;
    }
    //perform horizon culling
    if (face.level!=0 && !inHorizon(face)) return;
    if (!face.AnyChildrenNull())
    {
        vvec3 norm = face.GetNormal();
        unsigned int ni1,ni2,ni3; //new indices
        unsigned int currIndex=newVertices.size();
        if (face.level==0)
        {
            newVertices.push_back(Vertex(face.vertices[0], norm));
            newVertices.push_back(Vertex(face.vertices[1], norm));
            newVertices.push_back(Vertex(face.vertices[2], norm));
            index1 = currIndex + 0;
            index2 = currIndex + 1;
            index3 = currIndex + 2;
        }
        currIndex = newVertices.size();
        
        vvec3 norm0 = face.children[0]->GetNormal();
        vvec3 norm1 = face.children[1]->GetNormal();
        vvec3 norm2 = face.children[2]->GetNormal();
        vvec3 norm3 = face.children[3]->GetNormal();
        
        
        newVertices.push_back(Vertex(face.children[0]->vertices[0], glm::normalize(norm0 + norm1 + norm3)));
        newVertices.push_back(Vertex(face.children[0]->vertices[1], glm::normalize(norm0 + norm1 + norm2)));
        newVertices.push_back(Vertex(face.children[0]->vertices[2], glm::normalize(norm0 + norm2 + norm3)));
        ni1 = currIndex + 0;
        ni2 = currIndex + 1;
        ni3 = currIndex + 2;
        
        recursiveUpdate(*face.children[0], ni1, ni2, ni3, player, newVertices, newIndices);
        recursiveUpdate(*face.children[1], index3, ni1, ni3, player,newVertices, newIndices);
        recursiveUpdate(*face.children[2], ni3, ni2, index2, player,newVertices, newIndices);
        recursiveUpdate(*face.children[3], ni1, index1,ni2, player,newVertices, newIndices);
    }
    else
    {
        newIndices.push_back(index1);
        newIndices.push_back(index2);
        newIndices.push_back(index3);
    }
}


bool Planet::recursiveSubdivide(Face* face, Player& player)
{
    if (trySubdivide(face,
                     [this](Player& player, Face f)->bool {
                         return std::max(std::max(
                                                  glm::length(GetPlayerDisplacement() - f.vertices[0]),
                                                  glm::length(GetPlayerDisplacement() - f.vertices[1])),
                                         glm::length(GetPlayerDisplacement() - f.vertices[2]))
                         < (float)(1 << LOD_MULTIPLIER) / ((float)(1 << (f.level))); }
                     , player))
    {
        for (Face* f:face->children) recursiveSubdivide(f, player);
        return true;
    }
    return false;
}

bool Planet::recursiveCombine(Face* face, Player& player)
{
    if (closed) return false;
    if (face==nullptr) return false;
    if (!tryCombine(face,
                    
                    [this](Player& player, Face f)->bool { return std::min(std::min(
                                                                                    glm::length(GetPlayerDisplacement() - f.vertices[0]),
                                                                                    glm::length(GetPlayerDisplacement() - f.vertices[1])),
                                                                           glm::length(GetPlayerDisplacement() - f.vertices[2] - static_cast<vvec3>(Position)))
                        >= (double)(1 << (LOD_MULTIPLIER)) / ((double)(1 << (f.level-1))); }
                    
                    , player))
    {
        
        recursiveCombine(face->children[0], player) ||
                recursiveCombine(face->children[1], player) ||
                recursiveCombine(face->children[2], player) ||
                recursiveCombine(face->children[3], player);
        return false;
    }
    else return true;
}

void Planet::getRootFaces(std::vector<Face*>& rootFaces)
{
    for (Face& f:faces)
        recursiveGetRootFaces(rootFaces, &f);
}

void Planet::recursiveGetRootFaces(std::vector<Face *> &rootFaces, Face* f)
{
    if (!inHorizon(*f) && f->level!=0) return;
    for (auto& i:f->indices)i=-1;
    if (f->AllChildrenNull())
        rootFaces.push_back(f);
    else
        for (Face* child:f->children)
            recursiveGetRootFaces(rootFaces, child);
}

void Planet::updateVBO(Player& player)
{
    auto t = std::chrono::high_resolution_clock::now();
    unsigned int indsize, vertsize;
    GetIndicesVerticesSizes(indsize, vertsize);
    std::vector<Vertex> newVertices;
    newVertices.reserve(vertsize);
    std::vector<unsigned int> newIndices;
    newIndices.reserve(indsize);
    player.DistFromSurface=10.;
    
#ifdef SMOOTH_FACES
    
    std::vector<Face*> rootFaces;
    getRootFaces(rootFaces);
    
    
    std::vector<std::pair<int, int>> verticesSorted(rootFaces.size()*3);
    
    for (int i = 0; i<rootFaces.size();i++)
    {
        verticesSorted[3*i] = std::pair<int, int>(0,i);
        verticesSorted[3*i+1] = std::pair<int, int>(1,i);
        verticesSorted[3*i+2] = std::pair<int, int>(2,i);
    }
    
    
    
    std::sort(verticesSorted.begin(),verticesSorted.end(),[this, &rootFaces](std::pair<int, int>& p1, std::pair<int, int>& p2)
              {
                  glm::vec2 c1=rootFaces[p1.second]->polarCoords[p1.first] + glm::vec2(M_PI,M_2_PI);
                  glm::vec2 c2=rootFaces[p2.second]->polarCoords[p2.first] + glm::vec2(M_PI,M_2_PI);
                  
//                  return c1.x + 2 * Radius * c1.y + 4 * Radius * Radius * c1.z <
//                  c2.x + 2 * Radius * c2.y + 4 * Radius * Radius * c2.z;
                  return c1.x + 10 * c1.y < c2.x + 10*c2.y;
              });
    
    
    for (int j = 0; j<verticesSorted.size();j++)
    {
        const int maxNeighbors=6;
        
        if (rootFaces.at(verticesSorted[j].second)->indices.at(verticesSorted[j].first)!=-1) continue;
        int currentSize = newVertices.size();
        
        vvec3 normal = rootFaces.at(verticesSorted[j].second)->GetNormal();
        
        newVertices.push_back(Vertex(rootFaces.at(verticesSorted[j].second)->vertices.at(verticesSorted[j].first),glm::vec3()));
        
        rootFaces.at(verticesSorted[j].second)->indices.at(verticesSorted[j].first)=currentSize;
        int neig=0;
        
        for (int k = std::max(0, j-maxNeighbors);k<std::min((int)verticesSorted.size(),j+maxNeighbors);k++)
        {
            if (neig>maxNeighbors) break;
            if (k==j) continue;
            if (rootFaces.at(verticesSorted[k].second)->indices.at(verticesSorted[k].first)!=-1) continue;
            vvec2 v1 =rootFaces.at(verticesSorted[k].second)->polarCoords.at(verticesSorted[k].first);
            vvec2 v2 = rootFaces.at(verticesSorted[j].second)->polarCoords.at(verticesSorted[j].first);
//            printf("v1: %f, %f, %f; %f, %f, %f\n", v1.x, v1.y, v1.z, v2.x, v2.y, v2.z);
            if (v1==v2)
            {
                rootFaces.at(verticesSorted[k].second)->indices.at(verticesSorted[k].first) = currentSize;
                normal+=rootFaces.at(verticesSorted[k].second)->GetNormal();
                neig++;
            }
        }
        //This corrects seams. It leads to other unintended artifacts, so it is currently disabled.
//        if (neig<maxNeighbors)
//        {
//            vvec3 disp = rootFaces.at(verticesSorted[j].second)->vertices[verticesSorted[j].first] - rootFaces.at(verticesSorted[j].second)->GetCenter();
//            newVertices[currentSize].SetPosition(newVertices[currentSize].GetPosition() + static_cast<vfloat>(0.01) * disp);
//        }
        normal/=(neig+1);
        newVertices[currentSize].SetNormal(normal);
    }
    
    for (Face* f:rootFaces)
    {
        if (f->indices[0]!=-1 && f->indices[1]!=-1 && f->indices[2]!=-1)
        {
            newIndices.push_back(f->indices[0]);
            newIndices.push_back(f->indices[1]);
            newIndices.push_back(f->indices[2]);
        }
    }
#else
    for (Face& f : faces)
        recursiveUpdate(f, 0, 0, 0, player, newVertices, newIndices);
#endif
    std::cout << std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - t).count() << "us\n";
    if (!closed)
    {
        std::lock_guard<std::mutex> lock(renderMutex);
        vertices = newVertices;
        indices = newIndices;
    }
}


void Planet::buildBaseMesh()
{
    //The first part of this function, which generates a list of coordinates of icosahedron vertices, is not mine.
    //I used code on a forum or website (unfortunately cannot find it again)
    vvec3 icosahedron[12];
    
    vvec2 icosahedronPolar[12];
    
    //reference angle for icosahedron vertices in radians -- used to calculate Cartesian coordinates of vertices
    double theta = 26.56505117707799 * M_PI / 180.0;
    
    double sine = std::sin(theta);
    double cosine = std::cos(theta);
    
    icosahedron[0] = vvec3(0.0f, 0.0f, -1.0f)*Radius; //bottom vertex
    //upper pentagon
    int i;
    double phi;
    for (i = 1, phi=M_PI/5.; i < 6; ++i,phi+=2.*M_PI/5.) {
        icosahedron[i] = vvec3(cosine * std::cos(phi), cosine * std::sin(phi), -sine)*Radius;
    }
    
    //lower pentagon
    for (i = 6, phi=0.; i < 11; ++i, phi+=2.*M_PI/5.) {
        icosahedron[i] = vvec3(cosine * std::cos(phi), cosine * std::sin(phi), sine)*Radius;
        
    }
    
    icosahedron[11] = vvec3(0.0, 0.0, 1.0)*Radius; // top vertex
    
    for (int i = 0; i<12;i++)
    {
        vfloat len = glm::length(icosahedron[i]);
        icosahedronPolar[i] = vvec2(std::acos(icosahedron[i].z / len), std::atan(icosahedron[i].y/icosahedron[i].x));
    }
    
    int faceIndices[] = {
        0,2,1,  0,3,2,  0,4,3,  0,5,4,  0,1,5,
        1,2,7,  2,3,8,  3,4,9,  4,5,10, 5,1,6,
        1,7,6,  2,8,7,  3,9,8,  4,10,9, 5,6,10,
        6,7,11, 7,8,11, 8,9,11, 9,10,11,10,6,11,
    };
    //generate 20 icosahedron faces
    for (int i = 0; i<20;i++)
    {
        faces.push_back(Face(icosahedron[faceIndices[3 * i + 0]],
                             icosahedron[faceIndices[3 * i + 1]],
                             icosahedron[faceIndices[3 * i + 2]],
                             icosahedronPolar[faceIndices[3 * i + 0]],
                             icosahedronPolar[faceIndices[3 * i + 1]],
                             icosahedronPolar[faceIndices[3 * i + 2]]));
    }
}

void Planet::Draw()
{
    
    atmosphere.Position = static_cast<glm::vec3>(Position);
    setUniforms();
    time+=MainGame_SDL::ElapsedMilliseconds/10.;
    
    //adjust opengl rendering mode according to CurrentRenderMode -- wireframe or solid
    switch (CurrentRenderMode)
    {
        case RenderMode::SOLID:
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            break;
        case RenderMode::WIRE:
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            break;
    }
    
    unsigned int indsize, vertsize;
    GetIndicesVerticesSizes(indsize, vertsize);
    if (prevVerticesSize!=vertsize)
    {
        std::lock_guard<std::mutex> lock(renderMutex);
        glBindBuffer(GL_ARRAY_BUFFER,VBO);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, IBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * vertices.size(), &vertices[0], GL_STREAM_DRAW);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(unsigned int) * indsize, &indices[0], GL_STREAM_DRAW);
        prevVerticesSize=vertsize;
    }
//    glDisable(GL_DEPTH_TEST);
//    glManager.Programs[1].Use();
//    atmosphere.Draw();
    
//
    if (vertsize >0 || vertsize > 64)
    {
        glManager.Programs[0].Use();
        std::lock_guard<std::mutex> lock(renderMutex);
        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, IBO);
        glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, (void*)0);
        glBindVertexArray(0);
    }
}

void Planet::UpdatePhysics(double timeStep)
{
    PhysicsObject::UpdatePhysics(timeStep);
    RotationMatrix=glm::rotate(vmat4(), Angle, glm::normalize(AngularVelocity));
    RotationMatrixInv=glm::inverse(RotationMatrix);//glm::rotate(vmat4(), -Angle, glm::normalize(AngularVelocity));
    Angle+=static_cast<vfloat>(timeStep) * glm::length(AngularVelocity);
    player.Camera.PlanetRotation=Angle;
}

void Planet::setUniforms()
{
    std::lock_guard<std::mutex> lock(renderMutex);
//    glManager.Programs[1].Use();
//    atmosphere.SetUniforms(glManager, *this);
//
//    glManager.Programs[1].SetFloat("fCameraHeight", len);
//    glManager.Programs[1].SetFloat("fCameraHeight2", len*len);
//    glManager.Programs[1].SetVector3("v3CameraPos", GetPlayerDisplacement());
//    
//    glManager.Programs[1].SetVector3("v3LightPos", glm::vec3(10*sin(angle), 10*cos(angle),0.0));
////    glManager.Programs[1].SetMatrix4("transformMatrix", glm::value_ptr(player.Camera.GetTransformMatrix()));
//    glManager.Programs[1].SetMatrix4("modelViewMatrix", glm::value_ptr(player.Camera.GetViewMatrix()));
//    glManager.Programs[1].SetMatrix4("projectionMatrix", glm::value_ptr(player.Camera.GetProjectionMatrix()));
    glManager.Programs[0].Use();
    glManager.Programs[0].SetFloat("time",time);
//    SeaLevel=-1;
//    SeaLevel=0.01;
    PlanetInfo.Radius = static_cast<float>(Radius);
    PlanetInfo.transformMatrix = player.Camera.GetTransformMatrix()*glm::translate(vmat4(), static_cast<vvec3>(Position))*RotationMatrix;
    glManager.UpdateBuffer("planet_info", &PlanetInfo, sizeof(PlanetInfo));
    glManager.Programs[0].SetVector3("sunDir", glm::vec3(RotationMatrixInv * glm::vec4(sin(0), cos(0),0.0,1.0)));
//    player.Camera.PlanetRotation = CurrentRotationMode==RotationMode::ROTATION ? time*ROTATION_RATE : 0.0;
}