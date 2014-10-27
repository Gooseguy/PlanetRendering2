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


//Constructor for planet.  Initializes VBO (experimental) and builds the base icosahedron mesh.
Planet::Planet(glm::vec3 pos, vfloat radius, double mass, vfloat seed, Player& _player, GLManager& _glManager) : Radius(radius), time(0), SEED(seed), CurrentRenderMode(RenderMode::SOLID), player(_player), glManager(_glManager), closed(false), CurrentRotationMode(RotationMode::NO_ROTATION), ROTATION_RATE(0.005f), SeaLevel(0.001f), atmosphere(pos, radius*1.01),
PhysicsObject(static_cast<glm::dvec3>(pos), mass)//5.972E24)
{
    
    generateBuffers();
    buildBaseMesh();
    //create a separate thread in which updates occur
    std::thread t(&Planet::Update, this);
    t.detach();
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
}


//This function accepts a boolean-valued function of displacement.  If the function is true, the function will divide the given face into four subfaces, each of which will be recursively subjected to the same subdivision scheme.  It is important that the input function terminates at a particular level of detail, or the program will crash.
bool Planet::trySubdivide(Face* iterator, const std::function<bool (Player&, const Face&)>& func, Player& player)
{
    //perform horizon culling
    if (iterator->level!=0 && (!inHorizon(iterator->v1) && !inHorizon(iterator->v2) && !inHorizon(iterator->v3))) return false;
    
    if (closed) return false;
    
    if (func(player, *iterator))
    {
        if (iterator->child0!=nullptr || iterator->child1!=nullptr || iterator->child2!=nullptr || iterator->child3!=nullptr)
            return true;
        
        
        //face vertices
        vvec3 v1 = iterator->v1;
        vvec3 v2 = iterator->v2;
        vvec3 v3 = iterator->v3;
        
        vvec3 nv1 = glm::normalize(v1);
        vvec3 nv2 = glm::normalize(v2);
        vvec3 nv3 = glm::normalize(v3);
        
        //lengths of face vertices
        vfloat l1 = glm::length(v1)/Radius;
        vfloat l2 = glm::length(v2)/Radius;
        vfloat l3 = glm::length(v3)/Radius;
        
        //normalized midpoints of face vertices
        vvec3 m12 = glm::normalize((nv1 + nv2) * (vfloat)0.5f);
        vvec3 m13 = glm::normalize((nv1 + nv3) * (vfloat)0.5f);
        vvec3 m23 = glm::normalize((nv2 + nv3) * (vfloat)0.5f);
        
        //height scale of terrain
        //proportional to 2^(-LOD) * nonlinear factor
        //the nonlinear factor is LOD^(TERRAIN_REGULARITY)
        //if the nonlinear factor is 1, the terrain is boring -- this is introduced to make higher-frequency noise more noticeable.
        vfloat fac =1./(vfloat)(1 << iterator->level)*std::pow((iterator->level+1), TERRAIN_REGULARITY);
        
        
        m12*=1 + terrainNoise(m12) * fac;
        m13*=1 + terrainNoise(m13) * fac;
        m23*=1 + terrainNoise(m23) * fac;
        
        m12*=(l1 + l2)/2.*Radius;
        m13*=(l1 + l3)/2.*Radius;
        m23*=(l2 + l3)/2.*Radius;
        
//        m12+=Position;
//        m13+=Position;
//        m23+=Position;
        Face *f0,*f1,*f2,*f3;
        
        {
            std::lock_guard<std::mutex> lock(renderMutex);
            f0 = new Face(m13,m12,m23, iterator->level+1);
            f1 = new Face(v3,m13,m23,iterator->level+1);
            f2 = new Face(m23,m12,v2,iterator->level+1);
            f3 = new Face(m13,v1,m12,iterator->level+1);
        }
        iterator->child0 = f0;
        iterator->child1 = f1;
        iterator->child2 = f2;
        iterator->child3 = f3;
        
        return true;
    }
    return false;
}
//Similarly to trySubdivide, this function combines four faces into a larger face if a boolean-valued function is statisfied.
bool Planet::tryCombine(Face* iterator, const std::function<bool (Player&, const Face&)>& func, Player& player)
{
    if (closed) return false;
    if (iterator==nullptr) return false;
    if (iterator->child0==nullptr || iterator->child1==nullptr || iterator->child2==nullptr || iterator->child3==nullptr) return false;
    if ((func(player, *iterator) || func(player, *iterator->child0)|| func(player, *iterator->child1) || func(player, *iterator->child2) || func(player, *iterator->child3)) && iterator->level>0)
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
    if (face->child0!=nullptr)
    {
        combineFace(face->child0);
        delete face->child0;
        face->child0 = nullptr;
    }
    if (face->child1!=nullptr)
    {
        combineFace(face->child1);
        delete face->child1;
        face->child1 = nullptr;
    }
    if (face->child2!=nullptr)
    {
        combineFace(face->child2);
        delete face->child2;
        face->child2 = nullptr;
    }
    if (face->child3!=nullptr)
    {
        combineFace(face->child3);
        delete face->child3;
        face->child3 = nullptr;
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

void Planet::recursiveUpdate(Face& face, unsigned int index1, unsigned int index2, unsigned int index3, Player& player, std::vector<Vertex>& newVertices, std::vector<unsigned int>& newIndices)
{
    if (closed) return;
    //Get player distance from face.  If temporary minimum, set player distance (this process naturally finds the player's minimum distance to the surface).
    {
        std::lock_guard<std::mutex> lock(renderMutex);
        vfloat dist = std::min(std::min(glm::length(GetPlayerDisplacement() - face.v1),glm::length(GetPlayerDisplacement() - face.v2)),glm::length(GetPlayerDisplacement()- face.v3));
        if (player.DistFromSurface > dist) player.DistFromSurface = dist;
    }
    //perform horizon culling
    if (face.level!=0 && (!inHorizon(face.v1) && !inHorizon(face.v2) && !inHorizon(face.v3))) return;
    if (face.child0!=nullptr && face.child1!=nullptr && face.child2!=nullptr && face.child3!=nullptr)
    {
        vvec3 norm = face.GetNormal();
        unsigned int ni1,ni2,ni3; //new indices
        unsigned int currIndex=newVertices.size();
        if (face.level==0)
        {
            newVertices.push_back(Vertex(face.v1, norm));
            newVertices.push_back(Vertex(face.v2, norm));
            newVertices.push_back(Vertex(face.v3, norm));
            index1 = currIndex + 0;
            index2 = currIndex + 1;
            index3 = currIndex + 2;
        }
        currIndex = newVertices.size();
        
        vvec3 norm0 = face.child0->GetNormal();
        vvec3 norm1 = face.child1->GetNormal();
        vvec3 norm2 = face.child2->GetNormal();
        vvec3 norm3 = face.child3->GetNormal();
        
        
        newVertices.push_back(Vertex(face.child0->v1, glm::normalize(norm0 + norm1 + norm3)));
        newVertices.push_back(Vertex(face.child0->v2, glm::normalize(norm0 + norm1 + norm2)));
        newVertices.push_back(Vertex(face.child0->v3, glm::normalize(norm0 + norm2 + norm3)));
        ni1 = currIndex + 0;
        ni2 = currIndex + 1;
        ni3 = currIndex + 2;
        
        recursiveUpdate(*face.child0, ni1, ni2, ni3, player, newVertices, newIndices);
        recursiveUpdate(*face.child1, index3, ni1, ni3, player,newVertices, newIndices);
        recursiveUpdate(*face.child2, ni3, ni2, index2, player,newVertices, newIndices);
        recursiveUpdate(*face.child3, ni1, index1,ni2, player,newVertices, newIndices);
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
                                                  glm::length(GetPlayerDisplacement() - f.v1),
                                                  glm::length(GetPlayerDisplacement() - f.v2)),
                                         glm::length(GetPlayerDisplacement() - f.v3))
                         < (float)(1 << LOD_MULTIPLIER) / ((float)(1 << (f.level))); }
                     , player))
    {
        recursiveSubdivide(face->child0,player);
        recursiveSubdivide(face->child1,player);
        recursiveSubdivide(face->child2,player);
        recursiveSubdivide(face->child3,player);
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
                                                                                    glm::length(GetPlayerDisplacement() - f.v1),
                                                                                    glm::length(GetPlayerDisplacement() - f.v2)),
                                                                           glm::length(GetPlayerDisplacement() - f.v3 - static_cast<vvec3>(Position)))
                        >= (double)(1 << (LOD_MULTIPLIER)) / ((double)(1 << (f.level-1))); }
                    
                    , player))
    {
        
        recursiveCombine(face->child0, player) ||
                recursiveCombine(face->child1, player) ||
                recursiveCombine(face->child2, player) ||
        recursiveCombine(face->child3, player);
        return false;
    }
    else return true;
}

void Planet::updateVBO(Player& player)
{
    unsigned int indsize, vertsize;
    GetIndicesVerticesSizes(indsize, vertsize);
    std::vector<Vertex> newVertices;
    newVertices.reserve(vertsize);
    std::vector<unsigned int> newIndices;
    newIndices.reserve(indsize);
    player.DistFromSurface=10.;
    for (Face& f : faces)
        recursiveUpdate(f, 0, 0, 0, player, newVertices, newIndices);
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
    
    //generate 20 icosahedron vertices
    faces.push_back(Face(icosahedron[0], icosahedron[2], icosahedron[1]));
    faces.push_back(Face(icosahedron[0], icosahedron[3], icosahedron[2]));
    faces.push_back(Face(icosahedron[0], icosahedron[4], icosahedron[3]));
    faces.push_back(Face(icosahedron[0], icosahedron[5], icosahedron[4]));
    faces.push_back(Face(icosahedron[0], icosahedron[1], icosahedron[5]));
    
    faces.push_back(Face(icosahedron[1], icosahedron[2], icosahedron[7]));
    faces.push_back(Face(icosahedron[2], icosahedron[3], icosahedron[8]));
    faces.push_back(Face(icosahedron[3], icosahedron[4], icosahedron[9]));
    faces.push_back(Face(icosahedron[4], icosahedron[5], icosahedron[10]));
    faces.push_back(Face(icosahedron[5], icosahedron[1], icosahedron[6]));
    
    faces.push_back(Face(icosahedron[1], icosahedron[7], icosahedron[6]));
    faces.push_back(Face(icosahedron[2], icosahedron[8], icosahedron[7]));
    faces.push_back(Face(icosahedron[3], icosahedron[9], icosahedron[8]));
    faces.push_back(Face(icosahedron[4], icosahedron[10], icosahedron[9]));
    faces.push_back(Face(icosahedron[5], icosahedron[6], icosahedron[10]));
    
    faces.push_back(Face(icosahedron[6], icosahedron[7], icosahedron[11]));
    faces.push_back(Face(icosahedron[7], icosahedron[8], icosahedron[11]));
    faces.push_back(Face(icosahedron[8], icosahedron[9], icosahedron[11]));
    faces.push_back(Face(icosahedron[9], icosahedron[10], icosahedron[11]));
    faces.push_back(Face(icosahedron[10], icosahedron[6], icosahedron[11]));
    
}

void Planet::Draw()
{
    
    atmosphere.Position = static_cast<glm::vec3>(Position);
    setUniforms();
    time+=MainGame_SDL::ElapsedMilliseconds;
    
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
void Planet::setUniforms()
{
    std::lock_guard<std::mutex> lock(renderMutex);
    glManager.Programs[1].Use();
    atmosphere.SetUniforms(glManager, *this);
    
    float angle = (CurrentRotationMode == RotationMode::ROTATION ? 1 : -1) * time * ROTATION_RATE * M_PI / 180.;
    float len =glm::length(GetPlayerDisplacement())-1;
    glManager.Programs[1].SetFloat("fCameraHeight", len);
    glManager.Programs[1].SetFloat("fCameraHeight2", len*len);
    glManager.Programs[1].SetVector3("v3CameraPos", GetPlayerDisplacement());
    
    glManager.Programs[1].SetVector3("v3LightPos", glm::vec3(10*sin(angle), 10*cos(angle),0.0));
//    glManager.Programs[1].SetMatrix4("transformMatrix", glm::value_ptr(player.Camera.GetTransformMatrix()));
    glManager.Programs[1].SetMatrix4("modelViewMatrix", glm::value_ptr(player.Camera.GetViewMatrix()));
    glManager.Programs[1].SetMatrix4("projectionMatrix", glm::value_ptr(player.Camera.GetProjectionMatrix()));
    glManager.Programs[0].Use();
    glManager.Programs[0].SetMatrix4("transformMatrix", glm::value_ptr(player.Camera.GetTransformMatrix()*glm::translate(vmat4(), static_cast<vvec3>(Position))));
    glManager.Programs[0].SetFloat("time",time);
    glManager.Programs[0].SetFloat("seaLevel", SeaLevel);
    glManager.Programs[0].SetVector3("origin", Position);
    
    glManager.Programs[0].SetVector3("sunDir", glm::vec3(sin(angle), cos(angle),0.0));
    player.Camera.PlanetRotation = CurrentRotationMode==RotationMode::ROTATION ? time*ROTATION_RATE : 0.0;
}