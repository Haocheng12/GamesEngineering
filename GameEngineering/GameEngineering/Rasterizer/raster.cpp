#include <iostream>
#define _USE_MATH_DEFINES
#include <cmath>
#include <algorithm>
#include <chrono>


#include "GamesEngineeringBase.h" // Include the GamesEngineeringBase header
#include "matrix.h"
#include "colour.h"
#include "mesh.h"
#include "zbuffer.h"
#include "renderer.h"
#include "RNG.h"
#include "light.h"
#include "triangle.h"
#include <mutex>
#include <thread>

// Main rendering function that processes a mesh, transforms its vertices, applies lighting, and draws triangles on the canvas.
// Input Variables:
// - renderer: The Renderer object used for drawing.
// - mesh: Pointer to the Mesh object containing vertices and triangles to render.
// - camera: Matrix representing the camera's transformation.
// - L: Light object representing the lighting parameters.
void render(Renderer& renderer, Mesh* mesh, matrix& camera, Light& L) {
    // Combine perspective, camera, and world transformations for the mesh
    matrix p = renderer.perspective * camera * mesh->world;

    // Iterate through all triangles in the mesh
    for (triIndices& ind : mesh->triangles) {
        Vertex t[3]; // Temporary array to store transformed triangle vertices

        // Transform each vertex of the triangle
        for (unsigned int i = 0; i < 3; i++) {
            t[i].p = p * mesh->vertices[ind.v[i]].p; // Apply transformations
            t[i].p.divideW(); // Perspective division to normalize coordinates

            // Transform normals into world space for accurate lighting
            // no need for perspective correction as no shearing or non-uniform scaling
            t[i].normal = mesh->world * mesh->vertices[ind.v[i]].normal; 
            t[i].normal.normalise();

            // Map normalized device coordinates to screen space
            t[i].p[0] = (t[i].p[0] + 1.f) * 0.5f * static_cast<float>(renderer.canvas.getWidth());
            t[i].p[1] = (t[i].p[1] + 1.f) * 0.5f * static_cast<float>(renderer.canvas.getHeight());
            t[i].p[1] = renderer.canvas.getHeight() - t[i].p[1]; // Invert y-axis

            // Copy vertex colours
            t[i].rgb = mesh->vertices[ind.v[i]].rgb;
        }

        // Clip triangles with Z-values outside [-1, 1]
        if (fabs(t[0].p[2]) > 1.0f || fabs(t[1].p[2]) > 1.0f || fabs(t[2].p[2]) > 1.0f) continue;

        // Create a triangle object and render it
        triangle tri(t[0], t[1], t[2]);
        tri.draw(renderer, L, mesh->ka, mesh->kd);
    }
}

void cullingRender(Renderer& renderer, Mesh* mesh, matrix& camera, Light& L)
{
    // 1) FIRST: Perform MESH-LEVEL FRUSTUM CULLING (bounding sphere check)

    // We'll define a near plane distance (nearDist). 
    // For a typical perspective, you might define nearDist = 0.1f or 1.0f, etc.
    float nearDist = 1.0f;

    // Transform the mesh's bounding-sphere center into camera space
    //    cw = camera * mesh->world
    // but we only need it for the center, so:
    vec4 centerCam = camera * mesh->world * mesh->boundingCenter;
    if (-centerCam[2] < nearDist - mesh->boundingRadius) {
        // Entire sphere is behind near plane => skip
        return;
    }

    

    // If you want to be more thorough, you can define a far plane (z = -farDist)
    // and skip if (-centerCam.z > farDist + mesh->boundingRadius). 
    // Similarly, you can do left/right/top/bottom plane checks for a complete frustum test.
    //
    // For brevity, we'll keep just this near-plane test as a demo.

    // 2) PREPARE MATRICES FOR BACKFACE CULLING + PROJECTION
    matrix cw = camera * mesh->world;             // transform to camera space
    matrix p = renderer.perspective * cw;        // then to clip space (NDC)

    // 3) ITERATE THROUGH ALL TRIANGLES
    for (triIndices& ind : mesh->triangles)
    {
        //-------------------------------------------------------
        // A) BACKFACE CULLING in CAMERA SPACE
        //-------------------------------------------------------
        // Transform each vertex to camera space
        vec4 c0 = cw * mesh->vertices[ind.v[0]].p;
        vec4 c1 = cw * mesh->vertices[ind.v[1]].p;
        vec4 c2 = cw * mesh->vertices[ind.v[2]].p;

        // Convert them to vec3 for cross product
        vec3 v0(c0[0], c0[1], c0[2]);
        vec3 v1(c1[0], c1[1], c1[2]);
        vec3 v2(c2[0], c2[1], c2[2]);

        // Edges in camera space
        vec3 e1 = v1 - v0;
        vec3 e2 = v2 - v0;

        // Cross product
        vec3 cross = e1.cross(e2);

        // For CCW geometry in a typical right-handed system looking down -Z,
        // cross.z > 0 => back-facing.  If your model disappears, flip the sign check.
        if (cross.z > 0.0f)
        {
            continue; // Skip back-facing triangles
        }

        //-------------------------------------------------------
        // B) PERSPECTIVE + SCREEN-SPACE TRANSFORM
        //-------------------------------------------------------
        Vertex t[3];
        for (unsigned int i = 0; i < 3; i++)
        {
            // Clip-space transform
            t[i].p = p * mesh->vertices[ind.v[i]].p;
            t[i].p.divideW(); // perspective divide

            // Transform normals into world space for lighting
            t[i].normal = mesh->world * mesh->vertices[ind.v[i]].normal;
            t[i].normal.normalise();

            // Convert NDC -> pixel coords
            t[i].p[0] = (t[i].p[0] + 1.f) * 0.5f * (float)renderer.canvas.getWidth();
            t[i].p[1] = (t[i].p[1] + 1.f) * 0.5f * (float)renderer.canvas.getHeight();
            t[i].p[1] = renderer.canvas.getHeight() - t[i].p[1]; // Flip Y

            // Copy vertex color
            t[i].rgb = mesh->vertices[ind.v[i]].rgb;
        }

        //-------------------------------------------------------
        // C) SIMPLE Z-CLIP
        //-------------------------------------------------------
        // If any vertex has |z| > 1 => skip triangle
        if (fabs(t[0].p[2]) > 1.0f ||
            fabs(t[1].p[2]) > 1.0f ||
            fabs(t[2].p[2]) > 1.0f)
        {
            continue;
        }

        //-------------------------------------------------------
        // D) DRAW THE TRIANGLE
        //-------------------------------------------------------
        triangle tri(t[0], t[1], t[2]);
        tri.draw(renderer, L, mesh->ka, mesh->kd);
    }
}

unsigned int numThreads = 8;

void renderMT(Renderer& renderer, Mesh* mesh, matrix& camera, Light& L, int numThreads) {
    // Combine perspective, camera, and world transformations for the mesh
    matrix p = renderer.perspective * camera * mesh->world;

    std::mutex renderMutex; // Mutex for synchronizing renderer access

    // Lambda function for processing a subset of triangles
    auto processTriangles = [&](size_t start, size_t end) {
        for (size_t i = start; i < end; i++) {
            triIndices& ind = mesh->triangles[i];
            Vertex t[3]; // Temporary array to store transformed triangle vertices

            // Transform each vertex of the triangle
            for (unsigned int j = 0; j < 3; j++) {
                t[j].p = p * mesh->vertices[ind.v[j]].p; // Apply transformations
                t[j].p.divideW(); // Perspective division to normalize coordinates

                // Transform normals into world space for accurate lighting
                t[j].normal = mesh->world * mesh->vertices[ind.v[j]].normal;
                t[j].normal.normalise();

                // Map normalized device coordinates to screen space
                t[j].p[0] = (t[j].p[0] + 1.f) * 0.5f * static_cast<float>(renderer.canvas.getWidth());
                t[j].p[1] = (t[j].p[1] + 1.f) * 0.5f * static_cast<float>(renderer.canvas.getHeight());
                t[j].p[1] = renderer.canvas.getHeight() - t[j].p[1]; // Invert y-axis

                // Copy vertex colors
                t[j].rgb = mesh->vertices[ind.v[j]].rgb;
            }

            // Clip triangles with Z-values outside [-1, 1]
            if (fabs(t[0].p[2]) > 1.0f || fabs(t[1].p[2]) > 1.0f || fabs(t[2].p[2]) > 1.0f) continue;

            // Create a triangle object
            triangle tri(t[0], t[1], t[2]);

            // Lock the renderer before drawing
            {
                std::lock_guard<std::mutex> lock(renderMutex);
                tri.draw(renderer, L, mesh->ka, mesh->kd);
            }
        }
        };

    size_t totalTriangles = mesh->triangles.size();
    std::vector<std::thread> threads;
    size_t batchSize = totalTriangles / numThreads;
    size_t remaining = totalTriangles % numThreads;

    size_t start = 0;
    for (int i = 0; i < numThreads; i++) {
        size_t end = start + batchSize + (i < remaining ? 1 : 0); // Distribute remainder triangles evenly
        threads.emplace_back(processTriangles, start, end);
        start = end;
    }

    // Join threads to ensure all processing completes
    for (auto& t : threads) {
        t.join();
    }
}





// Test scene function to demonstrate rendering with user-controlled transformations
// No input variables
void sceneTest() {
    Renderer renderer;
    // create light source {direction, diffuse intensity, ambient intensity}
    Light L{ vec4(0.f, 1.f, 1.f, 0.f), colour(1.0f, 1.0f, 1.0f), colour(0.1f, 0.1f, 0.1f) };
    // camera is just a matrix
    matrix camera = matrix::makeIdentity(); // Initialize the camera with identity matrix

    bool running = true; // Main loop control variable

    std::vector<Mesh*> scene; // Vector to store scene objects

    // Create a sphere and a rectangle mesh
    Mesh mesh = Mesh::makeSphere(1.0f, 10, 20);
    //Mesh mesh2 = Mesh::makeRectangle(-2, -1, 2, 1);

    // add meshes to scene
    scene.push_back(&mesh);
   // scene.push_back(&mesh2); 

    float x = 0.0f, y = 0.0f, z = -4.0f; // Initial translation parameters
    mesh.world = matrix::makeTranslation(x, y, z);
    //mesh2.world = matrix::makeTranslation(x, y, z) * matrix::makeRotateX(0.01f);

    // Main rendering loop
    while (running) {
        renderer.canvas.checkInput(); // Handle user input
        renderer.clear(); // Clear the canvas for the next frame

        // Apply transformations to the meshes
     //   mesh2.world = matrix::makeTranslation(x, y, z) * matrix::makeRotateX(0.01f);
        mesh.world = matrix::makeTranslation(x, y, z);

        // Handle user inputs for transformations
        if (renderer.canvas.keyPressed(VK_ESCAPE)) break;
        if (renderer.canvas.keyPressed('A')) x += -0.1f;
        if (renderer.canvas.keyPressed('D')) x += 0.1f;
        if (renderer.canvas.keyPressed('W')) y += 0.1f;
        if (renderer.canvas.keyPressed('S')) y += -0.1f;
        if (renderer.canvas.keyPressed('Q')) z += 0.1f;
        if (renderer.canvas.keyPressed('E')) z += -0.1f;

        // Render each object in the scene
        for (auto& m : scene)
            render(renderer, m, camera, L);

        renderer.present(); // Display the rendered frame
    }
}

// Utility function to generate a random rotation matrix
// No input variables
matrix makeRandomRotation() {
    RandomNumberGenerator& rng = RandomNumberGenerator::getInstance();
    unsigned int r = rng.getRandomInt(0, 3);

    switch (r) {
    case 0: return matrix::makeRotateX(rng.getRandomFloat(0.f, 2.0f * M_PI));
    case 1: return matrix::makeRotateY(rng.getRandomFloat(0.f, 2.0f * M_PI));
    case 2: return matrix::makeRotateZ(rng.getRandomFloat(0.f, 2.0f * M_PI));
    default: return matrix::makeIdentity();
    }
}

// Function to render a scene with multiple objects and dynamic transformations
// No input variables
void scene1() {
    Renderer renderer;
    matrix camera;
    Light L{ vec4(0.f, 1.f, 1.f, 0.f), colour(1.0f, 1.0f, 1.0f), colour(0.1f, 0.1f, 0.1f) };

    bool running = true;

    std::vector<Mesh*> scene;

    // Create a scene of 40 cubes with random rotations
    for (unsigned int i = 0; i < 20; i++) {
        Mesh* m = new Mesh();
        *m = Mesh::makeCube(1.f);
        m->world = matrix::makeTranslation(-2.0f, 0.0f, (-3 * static_cast<float>(i))) * makeRandomRotation();
        scene.push_back(m);
        m = new Mesh();
        *m = Mesh::makeCube(1.f);
        m->world = matrix::makeTranslation(2.0f, 0.0f, (-3 * static_cast<float>(i))) * makeRandomRotation();
        scene.push_back(m);
    }

    float zoffset = 8.0f; // Initial camera Z-offset
    float step = -0.1f;  // Step size for camera movement

    auto start = std::chrono::high_resolution_clock::now();
    std::chrono::time_point<std::chrono::high_resolution_clock> end;
    int cycle = 0;

    // Main rendering loop
    while (running) {
        renderer.canvas.checkInput();
        renderer.clear();

        camera = matrix::makeTranslation(0, 0, -zoffset); // Update camera position

        // Rotate the first two cubes in the scene
        scene[0]->world = scene[0]->world * matrix::makeRotateXYZ(0.1f, 0.1f, 0.0f);
        scene[1]->world = scene[1]->world * matrix::makeRotateXYZ(0.0f, 0.1f, 0.2f);

        if (renderer.canvas.keyPressed(VK_ESCAPE)) break;

        zoffset += step;
        if (zoffset < -60.f || zoffset > 8.f) {
            step *= -1.f;
            if (++cycle % 2 == 0) {
                end = std::chrono::high_resolution_clock::now();
                std::cout << cycle / 2 << " :" << std::chrono::duration<double, std::milli>(end - start).count() << "ms\n";
                start = std::chrono::high_resolution_clock::now();
            }
        }

        for (auto& m : scene)
            //render(renderer, m, camera, L);
            //cullingRender(renderer, m, camera, L);
            renderMT(renderer, m, camera, L,numThreads);
        renderer.present();
    }

    for (auto& m : scene)
        delete m;
}

// Scene with a grid of cubes and a moving sphere
// No input variables
void scene2() {
    Renderer renderer;
    matrix camera = matrix::makeIdentity();
    Light L{ vec4(0.f, 1.f, 1.f, 0.f), colour(1.0f, 1.0f, 1.0f), colour(0.1f, 0.1f, 0.1f) };

    std::vector<Mesh*> scene;

    struct rRot { float x; float y; float z; }; // Structure to store random rotation parameters
    std::vector<rRot> rotations;

    RandomNumberGenerator& rng = RandomNumberGenerator::getInstance();

    // Create a grid of cubes with random rotations
    for (unsigned int y = 0; y < 6; y++) {
        for (unsigned int x = 0; x < 8; x++) {
            Mesh* m = new Mesh();
            *m = Mesh::makeCube(1.f);
            scene.push_back(m);
            m->world = matrix::makeTranslation(-7.0f + (static_cast<float>(x) * 2.f), 5.0f - (static_cast<float>(y) * 2.f), -8.f);
            rRot r{ rng.getRandomFloat(-.1f, .1f), rng.getRandomFloat(-.1f, .1f), rng.getRandomFloat(-.1f, .1f) };
            rotations.push_back(r);
        }
    }

    // Create a sphere and add it to the scene
    Mesh* sphere = new Mesh();
    *sphere = Mesh::makeSphere(1.0f, 10, 20);
    scene.push_back(sphere);
    float sphereOffset = -6.f;
    float sphereStep = 0.1f;
    sphere->world = matrix::makeTranslation(sphereOffset, 0.f, -6.f);

    auto start = std::chrono::high_resolution_clock::now();
    std::chrono::time_point<std::chrono::high_resolution_clock> end;
    int cycle = 0;

    bool running = true;
    while (running) {
        renderer.canvas.checkInput();
        renderer.clear();

        // Rotate each cube in the grid
        for (unsigned int i = 0; i < rotations.size(); i++)
            scene[i]->world = scene[i]->world * matrix::makeRotateXYZ(rotations[i].x, rotations[i].y, rotations[i].z);

        // Move the sphere back and forth
        sphereOffset += sphereStep;
        sphere->world = matrix::makeTranslation(sphereOffset, 0.f, -6.f);
        if (sphereOffset > 6.0f || sphereOffset < -6.0f) {
            sphereStep *= -1.f;
            if (++cycle % 2 == 0) {
                end = std::chrono::high_resolution_clock::now();
                std::cout << cycle / 2 << " :" << std::chrono::duration<double, std::milli>(end - start).count() << "ms\n";
                start = std::chrono::high_resolution_clock::now();
            }
        }

        if (renderer.canvas.keyPressed(VK_ESCAPE)) break;

        for (auto& m : scene)
            render(renderer, m, camera, L);
        renderer.present();
    }

    for (auto& m : scene)
        delete m;
}

struct RandRot {
    float rx;
    float ry;
    float rz;
};
void scene3()
{
    Renderer renderer;
    RandomNumberGenerator& rng = RandomNumberGenerator::getInstance();

    // Basic camera & lighting setup
    matrix camera = matrix::makeIdentity();
    Light L{
        vec4(0.f, 1.f, 1.f, 0.f),
        colour(1.0f, 1.0f, 1.0f),
        colour(0.1f, 0.1f, 0.1f)
    };

    // 3D box dimensions: for example, 4 x 4 x 4 = 64 cubes
    const unsigned int DIM = 8;

    // Spacing between cubes along each axis
    const float spacing = 2.5f;

    // Vector to store all cubes in the scene
    std::vector<Mesh*> scene;
    // Store a random per-cube rotation increment
    std::vector<RandRot> rotations;

    // Center the large cube shape around the origin by offsetting
    float startOffset = -((DIM - 1) * spacing) * 0.5f;

    // Create the cubes in a 3D loop
    for (unsigned int x = 0; x < DIM; x++)
    {
        for (unsigned int y = 0; y < DIM; y++)
        {
            for (unsigned int z = 0; z < DIM; z++)
            {
                Mesh* m = new Mesh();
                *m = Mesh::makeCube(1.0f);  // Each sub-cube is size 1

                // Position the sub-cube so that the entire group forms a larger cube
                float px = startOffset + x * spacing;
                float py = startOffset + y * spacing;
                float pz = startOffset + z * spacing;

                m->world = matrix::makeTranslation(px, py, pz);

                // Apply a random initial rotation
                m->world = m->world * makeRandomRotation();

                scene.push_back(m);

                // Random small rotation increments around X/Y/Z
                RandRot rr{
                    rng.getRandomFloat(-0.05f, 0.05f),
                    rng.getRandomFloat(-0.05f, 0.05f),
                    rng.getRandomFloat(-0.05f, 0.05f)
                };
                rotations.push_back(rr);
            }
        }
    }

    // Variables to move the camera in/out along the Z-axis
    float zoffset = 0.0f;
    float step = 0.2f;  // move speed for camera
    bool running = true;

    // For performance logging
    auto start = std::chrono::high_resolution_clock::now();
    int cycle = 0;

    while (running)
    {
        renderer.canvas.checkInput();
        renderer.clear();

        // Update camera position
        zoffset += step;
        // If we go too far, reverse direction and record performance times
        if (zoffset > 10.f || zoffset < -40.f)
        {
            step *= -1.f;

            // Every time we reverse direction, increment cycle
            // and every 2 cycles, output an average or single time in ms
            if (++cycle % 2 == 0)
            {
                auto end = std::chrono::high_resolution_clock::now();
                double ms = std::chrono::duration<double, std::milli>(end - start).count();
                std::cout << (cycle / 2) << " : " << ms << " ms\n";
                start = end;
            }
        }

        camera = matrix::makeTranslation(0.f, 0.f, -25.f - zoffset);

        // Rotate each sub-cube by its small random increments
        for (unsigned int i = 0; i < scene.size(); i++)
        {
            scene[i]->world =
                scene[i]->world *
                matrix::makeRotateXYZ(rotations[i].rx,
                    rotations[i].ry,
                    rotations[i].rz);
        }

        // Exit on ESC
        if (renderer.canvas.keyPressed(VK_ESCAPE))
        {
            running = false;
        }

        // Render all cubes
        for (auto& m : scene)
        {
            //render(renderer, m, camera, L);
            renderMT(renderer, m, camera, L,numThreads);
        }

        renderer.present();
    }

    // Cleanup
    for (auto& m : scene)
    {
        delete m;
    }
}




// Entry point of the application
// No input variables
int main() {
    // Uncomment the desired scene function to run
    scene1();
    //scene2();
    //scene3();
     
    

    return 0;
}