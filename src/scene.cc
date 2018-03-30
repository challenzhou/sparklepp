#include <array>
#include <vector>
#include "api/gpu_particle.h"

#include "_sprite.h"
#include "scene.h"
#include <iostream>
void Scene::init() {
  //init shaders.
  setup_shaders();

  //init particles.
  gpu_particle_ = new GPUParticle();
  gpu_particle_->init();
  //init geometry.
  setup_grid_geometry();

  setup_wirecube_geometry();
  setup_sphere_geometry();
  setup_texture();
  //set OpenGL rendering parameters.
  glClearColor(0.155f, 0.15f, 0.13f, 1.0f);
  glEnable(GL_PROGRAM_POINT_SIZE);

  glBlendEquation(GL_FUNC_ADD);
  //glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);

  CHECKGLERROR();
}

void Scene::deinit() {
  gpu_particle_->deinit();
  delete gpu_particle_;

  glDeleteVertexArrays(1u, &geo_.grid.vao);
  glDeleteVertexArrays(1u, &geo_.wirecube.vao);
  glDeleteVertexArrays(1u, &geo_.sphere.vao);
  glDeleteBuffers(1u, &geo_.grid.vbo);
  glDeleteBuffers(1u, &geo_.wirecube.vbo);
  glDeleteBuffers(1u, &geo_.wirecube.ibo);
  glDeleteBuffers(1u, &geo_.sphere.vbo);
}

void Scene::update(mat4x4 const &view, float const dt) {
  gpu_particle_->update(dt, view);
}

void Scene::render(mat4x4 const &view, mat4x4 const &viewProj) {

  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  //grid.
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  draw_grid(viewProj);

  //particle simulation.
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  //glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  //gpu_particle_->enable_sorting(true);//sorting phase occur during update.so before here.
  gpu_particle_->render(view, viewProj);

  //bounding and test volumes.
  glEnable(GL_DEPTH_TEST);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  mat4x4 mvp;
  vec4 color;

  //simulation bounding box.
  mat4x4_scale_iso(mvp, viewProj, gpu_particle_->simulation_box_size());
  vec4_set(color, 0.5f, 0.4f, 0.5f, 0.5f);
  draw_wirecube(mvp, color);

  //vector field bounding box.
  glm::vec3 const& dim = gpu_particle_->vectorfield_dimensions();
  mat4x4_scale_aniso(mvp, viewProj, dim.x, dim.y, dim.z);
  vec4_set(color, 0.5f, 0.5f, 0.1f, 0.3f);
  draw_wirecube(mvp, color);


  // Sphere
  /*mat4x4_dup(mvp, viewProj);
  float radius = 64.0f;
  mat4x4_translate_in_place(mvp, 30.0f, 110.0f, 0.0f);
  mat4x4_scale_iso(mvp, mvp, radius);
  vec4_set(color, 0.5f, 0.5f, 0.5f, 0.5f);
  draw_sphere(mvp, color);
  */
}

void Scene::setup_shaders() {
  //setup programs.
  char *src_buffer = new char[MAX_SHADER_BUFFERSIZE]();
  pgm_.basic = CompileProgram(SHADERS_DIR "/basic/vs_basic.glsl", SHADERS_DIR "/basic/fs_basic.glsl", src_buffer);
  LinkProgram(pgm_.basic, SHADERS_DIR "/basic/fs_basic.glsl");
  pgm_.grid = CompileProgram(SHADERS_DIR "/grid/vs_grid.glsl", SHADERS_DIR "/grid/fs_grid.glsl", src_buffer);
  LinkProgram(pgm_.grid, SHADERS_DIR "/grid/grid.glsl");
  delete[] src_buffer;

  //shaders uniform location.
  ulocation_.basic.color = GetUniformLocation(pgm_.basic, "uColor");
  ulocation_.basic.mvp = GetUniformLocation(pgm_.basic, "uMVP");
  ulocation_.grid.mvp = GetUniformLocation(pgm_.grid, "uMVP");
  ulocation_.grid.scaleFactor = GetUniformLocation(pgm_.grid, "uScaleFactor");
}

void Scene::setup_grid_geometry() {
  //size taken in world space.
  float const world_size = 1.0f;

  geo_.grid.resolution = 32u;//static_cast<unsigned int>(gpu_particle_->simulation_box_size()) / 2u;
  geo_.grid.nvertices = 4u * (geo_.grid.resolution + 1u);

  unsigned int const &res  = geo_.grid.resolution;
  unsigned int const &num_vertices = geo_.grid.nvertices;
  unsigned int const num_component = 2u;
  unsigned int const buffersize = num_vertices * num_component;
  std::vector<float> vertices(buffersize);

  float const cell_padding = world_size / geo_.grid.resolution;
  float const offset = cell_padding * (res/2.0f);

  for (size_t i = 0; i <= res; i++) {
    unsigned int const index = 4u * num_component * i;
    float const cursor = cell_padding * i - offset;

    //horizontal lines.
    vertices[index + 0u] = -offset;
    vertices[index + 1u] = cursor;
    vertices[index + 2u] = +offset;
    vertices[index + 3u] = cursor;

    //vertical lines.
    vertices[index + 4u] = cursor;
    vertices[index + 5u] = -offset;
    vertices[index + 6u] = cursor;
    vertices[index + 7u] = +offset;
  }
  //allocate storage.
  glGenBuffers (1u, &geo_.grid.vbo);
  glBindBuffer(GL_ARRAY_BUFFER, geo_.grid.vbo);
    size_t const bytesize = vertices.size() * sizeof(vertices[0u]);
    glBufferData(GL_ARRAY_BUFFER, bytesize, vertices.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0u);
  //set attribute locations.
  glGenVertexArrays(1u, &geo_.grid.vao);
  glBindVertexArray(geo_.grid.vao);
  {

    glBindBuffer(GL_ARRAY_BUFFER, geo_.grid.vbo);
    {
      size_t const attrib_pos = glGetAttribLocation(pgm_.grid, "inPosition");
      glVertexAttribPointer(attrib_pos, num_component, GL_FLOAT, GL_FALSE,0 , nullptr);
      glEnableVertexAttribArray(attrib_pos);

    }
  }
  glBindVertexArray(0u);
}

void Scene::setup_wirecube_geometry() {
  //setup the wireframe cube.
  float const world_size = 1.0f;
  float const c = 0.5f * world_size;

  std::array<float, 24> const vertices = {
    +c, +c, +c,   +c, -c, +c,   +c, -c, -c,   +c, +c, -c,
    -c, +c, +c,   -c, -c, +c,   -c, -c, -c,   -c, +c, -c
  };

  std::array<unsigned char, 24> const indices = {
    0, 1, 1, 2, 2, 3, 3, 0,
    4, 5, 5, 6, 6, 7, 7, 4,
    0, 4, 1, 5, 2, 6, 3, 7
  };

  //vertices storage.
  size_t bytesize(0u);
  glGenBuffers(1u, &geo_.wirecube.vbo);
  glBindBuffer(GL_ARRAY_BUFFER, geo_.wirecube.vbo);
    bytesize = vertices.size() * sizeof(vertices[0u]);
    glBufferData(GL_ARRAY_BUFFER, bytesize, vertices.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0u);

    //indices storage.
    glGenBuffers(1u, &geo_.wirecube.ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geo_.wirecube.ibo);
      bytesize = indices.size() * sizeof(indices[0u]);
      glBufferData(GL_ELEMENT_ARRAY_BUFFER, bytesize, vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0u);

    //rendering attributes.
    glGenVertexArrays(1u, &geo_.wirecube.vao);
    glBindVertexArray(geo_.wirecube.vao);
    {
      //Positions
      unsigned int const attrib_pos = glGetAttribLocation(pgm_.basic, "inPosition");

      glBindBuffer(GL_ARRAY_BUFFER, geo_.wirecube.vbo);
      glVertexAttribPointer(attrib_pos, 3, GL_FLOAT, GL_FALSE,0 , nullptr);
      glEnableVertexAttribArray(attrib_pos);
      //enable element array.
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geo_.wirecube.ibo);
    }
    glBindVertexArray(0u);

    //element array parameters.
    geo_.wirecube.nindices = static_cast<unsigned int>(indices.size());
    geo_.wirecube.indices_type = GL_UNSIGNED_BYTE;

    CHECKGLERROR();
  }

  void Scene::setup_sphere_geometry() {
    float const world_size = 2.0f;
    float const radius = 0.5f * world_size;

    geo_.sphere.resolution = 32u;
    geo_.sphere.nvertices = 2u * geo_.sphere.resolution * (geo_.sphere.resolution + 2u);

    float theta2, phi;    // next theta angle, phi angle
    float ct, st;         // cos(theta), sin(theta)
    float ct2, st2;       // cos(next theta), sin(next theta)
    float cp, sp;         // cos(phi), sin(phi)

    float const Pi = static_cast<float>(M_PI);
    float const TwoPi = 2.0f * Pi;
    float const  Delta = 1.0f / static_cast<float>(geo_.sphere.resolution);

    //trigonometry base value, base of the spirale sphere.
    ct2 = 0.0f; st2 = -1.0f;

    //vertices data.
    unsigned int const num_component = 3u;
    std::vector<float> vertices(num_component * geo_.sphere.nvertices);

    //create a sphere from bottom to top (like a spiral) as a tristrip.
    unsigned int id = 0u;
    for (size_t j = 0; j < geo_.sphere.resolution; j++) {
      ct = ct2;
      st =st2;

      theta2 = ((j+1u) * Delta - 0.5f) * Pi;
      ct2 = glm::cos(theta2);
      st2 = glm::sin(theta2);

      vertices[id++] = radius * (ct);
      vertices[id++] = radius * (st);
      vertices[id++] = 0.0f;

      for (size_t i = 0; i < geo_.sphere.resolution + 1u; i++) {
        phi = TwoPi * i * Delta;
        cp = glm::cos(phi);
        sp = glm::sin(phi);

        vertices[id++] = radius * (ct2 * cp);
        vertices[id++] = radius * (st2);
        vertices[id++] = radius * (ct2 * sp);

        vertices[id++] = radius * (ct * cp);
        vertices[id++] = radius * (st);
        vertices[id++] = radius * (ct * sp);
      }
      vertices[id++] = radius * (ct2);
      vertices[id++] = radius * (st2);
      vertices[id++] = 0.0f;
    }

    //allocate storage.
    glGenBuffers(1u, &geo_.sphere.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, geo_.sphere.vbo);
      size_t const bytesize = vertices.size() * sizeof(vertices[0u]);
      glBufferData(GL_ARRAY_BUFFER, bytesize, vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0u);

    //set attribute locations.
    glGenVertexArrays(1u, &geo_.sphere.vao);
    glBindVertexArray(geo_.sphere.vao);
    {
      glBindBuffer(GL_ARRAY_BUFFER, geo_.sphere.vbo);
      {
        unsigned int const attrib_pos =  glGetAttribLocation(pgm_.basic, "inPosition");
        glVertexAttribPointer(attrib_pos, num_component, GL_FLOAT, GL_FALSE,0 , nullptr);
        glEnableVertexAttribArray(attrib_pos);
      }
    }
    glBindVertexArray(0u);
  }

  void Scene::setup_texture() {
    unsigned int const w = sprite_width;
    unsigned int const h = sprite_height;

    char *pixels = new char[3u*w*h];
    char *texdata = new char[w*h];

    for (size_t i = 0; i < w*h; i++) {
      char *px = pixels + 3*i;
      HEADER_PIXEL(sprite_data, px);
      texdata[i] = *px;
    }
    delete[] pixels;

    glGenTextures(1u, &gl_sprite_tex_);
    glBindTexture(GL_TEXTURE_2D, gl_sprite_tex_);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexStorage2D(GL_TEXTURE_2D, 4u, GL_R8, w, h);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED, GL_UNSIGNED_BYTE, texdata);
      glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0u);

    delete[] texdata;

    // bind here just for testing.
    //glBindTexture(GL_TEXTURE_2D, gl_sprite_tex_);

    CHECKGLERROR();
  }

  void Scene::draw_grid(mat4x4 const &mvp) {
    glUseProgram(pgm_.grid);
    {
      glUniformMatrix4fv(ulocation_.grid.mvp, 1, GL_FALSE, (GLfloat *const) mvp);
      glUniform1f(ulocation_.grid.scaleFactor, gpu_particle_->simulation_box_size());

      glBindVertexArray(geo_.grid.vao);
        glDrawArrays(GL_LINES, 0, geo_.grid.nvertices);
      glBindVertexArray(0u);
    }
    glUseProgram(0u);

    CHECKGLERROR();
  }

  void Scene::draw_wirecube(mat4x4 const &mvp, vec4 const &color) {
    glUseProgram(pgm_.basic);
    {
      glUniformMatrix4fv(ulocation_.basic.mvp, 1, GL_FALSE, (GLfloat *const) mvp);
      glUniform4fv(ulocation_.basic.color, 1u, (GLfloat *const)  color);

      glBindVertexArray(geo_.wirecube.vao);
        glDrawElements(GL_LINES, geo_.wirecube.nindices, geo_.wirecube.indices_type, nullptr);
      glBindVertexArray(0u);
    }
    glUseProgram(0u);

    CHECKGLERROR();
  }

  void::Scene::draw_sphere(mat4x4 const &mvp, vec4 const &color) {
    glUseProgram(pgm_.basic);
    {
      glUniformMatrix4fv(ulocation_.basic.mvp, 1, GL_FALSE, (GLfloat *const) mvp);
      glUniform4fv(ulocation_.basic.color, 1u, (GLfloat *const) color);

      glBindVertexArray(geo_.sphere.vao);
        glDrawArrays(GL_LINES, 0, geo_.sphere.nvertices);
      glBindVertexArray(0u);
    }
    glUseProgram(0u);

    CHECKGLERROR();
  }
