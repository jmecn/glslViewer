#include <time.h>
#include <sys/time.h>
#include <sys/shm.h>
#include <sys/stat.h> 
#include <unistd.h>
#include <signal.h>
#include <map>

#include "platform.h"
#include "utils.h"
#include "shader.h"
#include "vertexLayout.h"
#include "vbo.h"
#include "texture.h"

// GLOBAL VARIABLES
//============================================================================
//

//  List of FILES to watch and the variable to communicate that between process
struct WatchFile {
    std::string type;
    std::string path;
    int lastChange;
};
std::vector<WatchFile> files;
int* iHasChanged;

bool haveExt(const std::string& file, const std::string& ext){
    return file.find("."+ext) != std::string::npos;
}

//  SHADER
Shader shader;
int iFrag = -1;
std::string fragSource = "";
int iVert = -1;
std::string vertSource =
"attribute vec4 a_position;\n"
"attribute vec2 a_texcoord;\n"
"varying vec2 v_texcoord;\n"
"void main(void) {\n"
"    gl_Position = a_position*0.5;\n"
"    v_texcoord = a_texcoord;\n"
"}\n";

//  ASSETS
Vbo* mesh;
int iGeom = -1;
std::map<std::string,Texture*> textures;

//  TIME
struct timeval tv;
unsigned long long timeStart;
unsigned long long timeNow;
float timeSec = 0.0f;
float timeLimit = 0.0f;

// Billboard
//============================================================================
Vbo* rect (float _x, float _y, float _w, float _h) {
    std::vector<VertexLayout::VertexAttrib> attribs;
    attribs.push_back({"a_position", 3, GL_FLOAT, false, 0});
    attribs.push_back({"a_texcoord", 2, GL_FLOAT, false, 0});
    VertexLayout* vertexLayout = new VertexLayout(attribs);

    struct PosUvVertex {
        GLfloat pos_x;
        GLfloat pos_y;
        GLfloat pos_z;
        GLfloat texcoord_x;
        GLfloat texcoord_y;
    };

    std::vector<PosUvVertex> vertices;
    std::vector<GLushort> indices;

    float x = _x-1.0f;
    float y = _y-1.0f;
    float w = _w*2.0f;
    float h = _h*2.0f;

    vertices.push_back({ x, y, 1.0, 0.0, 0.0 });
    vertices.push_back({ x+w, y, 1.0, 1.0, 0.0 });
    vertices.push_back({ x+w, y+h, 1.0, 1.0, 1.0 });
    vertices.push_back({ x, y+h, 1.0, 0.0, 1.0 });
    
    indices.push_back(0); indices.push_back(1); indices.push_back(2);
    indices.push_back(2); indices.push_back(3); indices.push_back(0);

    Vbo* tmpMesh = new Vbo(vertexLayout);
    tmpMesh->addVertices((GLbyte*)vertices.data(), vertices.size());
    tmpMesh->addIndices(indices.data(), indices.size());

    return tmpMesh;
}

// Rendering Thread
//============================================================================
void setup() {

    gettimeofday(&tv, NULL);
    timeStart = (unsigned long long)(tv.tv_sec) * 1000 +
                (unsigned long long)(tv.tv_usec) / 1000; 

    //  Build shader;
    //
    if ( iFrag != -1 ) {
        fragSource = "";
        if(!loadFromPath(files[iFrag].path, &fragSource)) {
            return;
        }
        if ( iVert != -1 ) {
            vertSource = "";
            if(!loadFromPath(files[iVert].path, &vertSource)) {
                return;
            }
        }
        shader.load(fragSource,vertSource);
    } 
    
    //  Load Geometry
    //
    if ( iGeom == -1){
        mesh = rect(0.0,0.0,1.0,1.0);
    } else {
        // TODO: 
    }
}

void checkChanges(){
    if(*iHasChanged != -1) {

        std::string type = files[*iHasChanged].type;
        std::string path = files[*iHasChanged].path;

        if ( type == "fragment" ){
            fragSource = "";
            if(loadFromPath(path, &fragSource)){
                shader.detach(GL_FRAGMENT_SHADER | GL_VERTEX_SHADER);
                shader.load(fragSource,vertSource);
            }
        } else if ( type == "vertex" ){
            vertSource = "";
            if(loadFromPath(path, &vertSource)){
                shader.detach(GL_FRAGMENT_SHADER | GL_VERTEX_SHADER);
                shader.load(fragSource,vertSource);
            }
        } else if ( type == "geometry" ){
            // TODO
        } else if ( type == "image" ){
            for (std::map<std::string,Texture*>::iterator it = textures.begin(); it!=textures.end(); ++it) {
                if ( path == it->second->getFilePath() ){
                    it->second->load(path);
                    break;
                }
            }
        }
    
        *iHasChanged = -1;
    }
}

void draw(){

    // Something change??
    checkChanges();

	gettimeofday(&tv, NULL);

    timeNow =   (unsigned long long)(tv.tv_sec) * 1000 +
                (unsigned long long)(tv.tv_usec) / 1000;

    timeSec = (timeNow - timeStart)*0.001;

    shader.use();
    shader.sendUniform("u_time", timeSec);
    shader.sendUniform("u_mouse", mouse.x, mouse.y);
    shader.sendUniform("u_resolution",viewport.width, viewport.height);

    unsigned int index = 0;
    for (std::map<std::string,Texture*>::iterator it = textures.begin(); it!=textures.end(); ++it) {
        shader.sendUniform(it->first,it->second,index);
        shader.sendUniform(it->first+"Resolution",it->second->getWidth(),it->second->getHeight());
        index++;
    }

    mesh->draw(&shader);
}

void exit_func(void){
    // clear screen
    glClear( GL_COLOR_BUFFER_BIT );

    // close openGL instance
    closeGL();

    // delete resources
    for (std::map<std::string,Texture*>::iterator i = textures.begin(); i != textures.end(); ++i) {
        delete i->second;
        i->second = NULL;
    }
    textures.clear();
    delete mesh;
}

void renderThread(int argc, char **argv) {
    // Prepare viewport
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT );
    
    resizeGL(viewport.width, viewport.height);

    // Setup
    setup();

    // Turn on Alpha blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

    // Clear the background
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    int textureCounter = 0;
    std::string outputFile = "";

    //Load the the resources (textures)
    for (int i = 1; i < argc ; i++){
        std::string argument = std::string(argv[i]);

        if (argument == "-x" || argument == "-y" || 
            argument == "-w" || argument == "--width" || 
            argument == "-h" || argument == "--height" ) {
            i++;
        } else if ( argument == "--square" ||
                    argument == "-l" || 
                    argument == "--life-coding"  ) {

        } else if ( argument == "-s" || argument == "--sec") {
            i++;
            timeLimit = getFloat(argument);
            std::cout << "Will exit in " << timeLimit << " seconds." << std::endl;

        } else if ( argument == "-o" ){
            i++;
            argument = std::string(argv[i]);
            if( haveExt(argument,"png") ){
                outputFile = argument;
                std::cout << "Will save screenshot to " << outputFile  << " on exit." << std::endl; 
            } else {
                std::cout << "At the moment screenshots only support PNG formats" << std::endl;
            }

        } else if (argument.find("-") == 0) {
            std::string parameterPair = argument.substr(argument.find_last_of('-')+1);

            i++;
            argument = std::string(argv[i]);
            Texture* tex = new Texture();
            if( tex->load(argument) ){
                textures[parameterPair] = tex;
                std::cout << "Loading " << argument << " as the following uniform: " << std::endl;
                std::cout << "    uniform sampler2D u_" << parameterPair  << "; // loaded"<< std::endl;
                std::cout << "    uniform vec2 u_" << parameterPair  << "Resolution;"<< std::endl;
            }

        } else if ( haveExt(argument,"png") || haveExt(argument,"PNG") ||
                    haveExt(argument,"jpg") || haveExt(argument,"JPG") || 
                    haveExt(argument,"jpeg") || haveExt(argument,"JPEG") ) {

            Texture* tex = new Texture();
            if( tex->load(argument) ){
                std::string name = "u_tex"+getString(textureCounter);
                textures[name] = tex;
                std::cout << "Loading " << argument << " as the following uniform: " << std::endl;
                std::cout << "    uniform sampler2D " << name  << "; // loaded"<< std::endl;
                std::cout << "    uniform vec2 " << name  << "Resolution;"<< std::endl;
                textureCounter++;
            }
        }
    }

    bool bPlay = true;
    // Render Loop
    while (isGL() && bPlay) {
        // Update
        updateGL();
        
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Draw
        draw();
        
        // Swap the buffers
        renderGL();

        if( (timeLimit > 0.0 && timeSec > timeLimit) ||
            keypress == 'q' || keypress == 'Q' ||
            keypress == 's' || keypress == 'S' ){

            if (outputFile != "") {
                unsigned char* pixels = new unsigned char[viewport.width*viewport.height*4];
                glReadPixels(0, 0, viewport.width, viewport.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
                Texture::savePixels(outputFile, pixels, viewport.width, viewport.height);
            }
        }

        if ((timeLimit > 0.0 && timeSec > timeLimit) || 
            keypress == 'q' || keypress == 'Q'){
            bPlay = false;
        }
    }
}

//  Watching Thread
//============================================================================
void watchThread() {
    struct stat st;
    while(1){
        for(int i = 0; i < files.size(); i++){
            if( *iHasChanged == -1 ){
                int ierr = stat(files[i].path.c_str(), &st);
                int date = st.st_mtime;
                if (date != files[i].lastChange ){
                    *iHasChanged = i;
                    files[i].lastChange = date;
                }
                usleep(500000);
            }
        }
    }
}

// Main program
//============================================================================
int main(int argc, char **argv){

    // Load files to watch
    struct stat st;
    for (int i = 1; i < argc ; i++){
        std::string argument = std::string(argv[i]);

        if ( iFrag == -1 && haveExt(argument,"frag") || haveExt(argument,"fs") ) {
            int ierr = stat(argument.c_str(), &st);
            if (ierr != 0) {
                    std::cerr << "Error watching file " << argv[i] << std::endl;
            } else {
                WatchFile file;
                file.type = "fragment";
                file.path = argument;
                file.lastChange = st.st_mtime;
                files.push_back(file);
                iFrag = files.size()-1;
            }
        } else if ( iVert == -1 && haveExt(argument,"vert") || haveExt(argument,"vs") ) {
            int ierr = stat(argument.c_str(), &st);
            if (ierr != 0) {
                    std::cerr << "Error watching file " << argument << std::endl;
            } else {
                WatchFile file;
                file.type = "vertex";
                file.path = argument;
                file.lastChange = st.st_mtime;
                files.push_back(file);
                iVert = files.size()-1;
            }
        } else if ( iGeom == -1 && haveExt(argument,"ply") ) {
            int ierr = stat(argument.c_str(), &st);
            if (ierr != 0) {
                    std::cerr << "Error watching file " << argument << std::endl;
            } else {
                WatchFile file;
                file.type = "geometry";
                file.path = argument;
                file.lastChange = st.st_mtime;
                files.push_back(file);
                iGeom = files.size()-1;
            }
        } else if ( haveExt(argument,"png") || haveExt(argument,"PNG") ||
                    haveExt(argument,"jpg") || haveExt(argument,"JPG") || 
                    haveExt(argument,"jpeg") || haveExt(argument,"JPEG") ){
            int ierr = stat(argument.c_str(), &st);
            if (ierr != 0) {
                    // std::cerr << "Error watching file " << argument << std::endl;
            } else {
                WatchFile file;
                file.type = "image";
                file.path = argument;
                file.lastChange = st.st_mtime;
                files.push_back(file);
            }
        }
    }

    // If no shader
    if( iFrag == -1 ) {
		std::cerr << "GLSL render that updates changes instantly.\n";
		std::cerr << "Usage: " << argv[0] << " shader.frag [texture.(png\\jpg)] [-textureNameA texture.png] [-u] [-x x] [-y y] [-w width] [-h height] [-l/--livecoding] [--square] [-s seconds] [-o screenshot.png]\n";
		return EXIT_FAILURE;
	}

    // Fork process with a shared variable
    //
    int shmId = shmget(IPC_PRIVATE, sizeof(bool), 0666);
    pid_t pid = fork();
    iHasChanged = (int *) shmat(shmId, NULL, 0);

    switch(pid) {
        case -1: //error
        break;

        case 0: // child
        {
            *iHasChanged = -1;
            watchThread();
        }
        break;

        default: 
        {
            // Initialize openGL context
            initGL(argc,argv);

            // OpenGL Render Loop
            renderThread(argc,argv);
            
            //  Kill the iNotify watcher once you finish
            kill(pid, SIGKILL);
            exit_func();
        }
        break;
    }
    
    shmctl(shmId, IPC_RMID, NULL);
    return 0;
}
