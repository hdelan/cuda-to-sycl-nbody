#include "gl.hpp"

#include "shader.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

const int FBO_MARGIN = 50;

using namespace std;

/// Provides the gl state with window dimensions for fbo size, etc
void set_window_dimensions(gl_state &state, int width, int height);

/// Generates the star flare texture
void create_flare_texture(gl_state &state);

/// Creates the VAO and VBO objects
void create_vaos_vbos(gl_state &state);

/// Loads the shaders into the gl state
void init_shaders(gl_state &state);

// Initializes and supplies the framebuffers with valid data
void init_fbos(gl_state &state);

// Supplies the gl state with nbody simulation parameters
void set_uniforms(gl_state &state, sim_param params);

void init(gl_state &state, int width, int height, sim_param params)
{
  set_window_dimensions(state, width, height);
  create_flare_texture(state);
  create_vaos_vbos(state);
  init_shaders(state);
  init_fbos(state);
  set_uniforms(state, params);
}

void set_window_dimensions(gl_state &state, int width, int height)
{
  state.width  = width;
  state.height = height;
}

void create_flare_texture(gl_state &state)
{
  int tex_size = 16;
  state.tex_size = tex_size;
  glCreateTextures(GL_TEXTURE_2D, 1, &state.flare_tex);
  glTextureStorage2D(state.flare_tex, 1, GL_R32F, tex_size, tex_size);
  glTextureParameteri(state.flare_tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  {
    std::vector<float> pixels(tex_size*tex_size);
    float sigma2 = tex_size/2.0;
    float A = 1.0;
    for (int i=0;i<tex_size;++i)
    {
      float i1 = i-tex_size/2;
      for (int j=0;j<tex_size;++j)
      {
        float j1 = j-tex_size/2;
        // gamma corrected gauss
        pixels[i*tex_size+j] = pow(A*exp(-((i1*i1)/(2*sigma2) + (j1*j1)/(2*sigma2))),2.2);
      }
    }
    glTextureSubImage2D(state.flare_tex, 0, 0, 0, 
      tex_size, tex_size, GL_RED, GL_FLOAT, pixels.data());
  }
}

void create_vaos_vbos(gl_state &state)
{
  // Particle VAO
  glCreateVertexArrays(1, &state.vao_particles);
  glCreateBuffers(1, &state.vbo_particles_pos);
  glCreateBuffers(1, &state.ssbo_velocities);
  glVertexArrayVertexBuffer(state.vao_particles, 0, state.vbo_particles_pos, 0, sizeof(glm::vec4));
  glVertexArrayVertexBuffer(state.vao_particles, 1, state.ssbo_velocities  , 0, sizeof(glm::vec4));

  // Position
  glEnableVertexArrayAttrib( state.vao_particles, 0);
  glVertexArrayAttribFormat( state.vao_particles, 0, 4, GL_FLOAT, GL_FALSE, 0);
  glVertexArrayAttribBinding(state.vao_particles, 0, 0);

  // Velocity
  glEnableVertexArrayAttrib( state.vao_particles, 1);
  glVertexArrayAttribFormat( state.vao_particles, 1, 4, GL_FLOAT, GL_FALSE, 0);
  glVertexArrayAttribBinding(state.vao_particles, 1, 1);

  // Deferred VAO
  glCreateVertexArrays(1, &state.vao_deferred);
  glCreateBuffers(1, &state.vbo_deferred);
  glVertexArrayVertexBuffer( state.vao_deferred, 0, state.vbo_deferred, 0, sizeof(glm::vec2));
  // Position
  glEnableVertexArrayAttrib( state.vao_deferred, 0);
  glVertexArrayAttribFormat( state.vao_deferred, 0, 2, GL_FLOAT, GL_FALSE, 0);
  glVertexArrayAttribBinding(state.vao_deferred, 0, 0);

  // Deferred tri
  glm::vec2 tri[3] = {
    glm::vec2(-2,-1), 
    glm::vec2(+2,-1),
    glm::vec2( 0, 4)};
  glNamedBufferStorage(state.vbo_deferred, 3*sizeof(glm::vec2), tri, 0);
}

void populate_particles(gl_state &state, const vector<glm::vec4> pos, const vector<glm::vec4> vel)
{
  // SSBO allocation & data upload
  glNamedBufferStorage(state.vbo_particles_pos, pos.size()*sizeof(glm::vec4), pos.data(), 0);
  glNamedBufferStorage(state.ssbo_velocities  , vel.size()*sizeof(glm::vec4), vel.data(), 0);

  // SSBO binding
  glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 0, state.vbo_particles_pos, 
    0, sizeof(glm::vec4)*pos.size());
  glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, state.ssbo_velocities,
    0, sizeof(glm::vec4)*vel.size());
}

void init_shaders(gl_state &state)
{
  state.program_interaction = new_program();
  program_source(state.program_interaction, GL_COMPUTE_SHADER, "interaction.comp");
  program_link  (state.program_interaction);

  state.program_integration = new_program();
  program_source(state.program_integration, GL_COMPUTE_SHADER, "integration.comp");
  program_link  (state.program_integration);

  state.program_hdr = new_program();
  program_source(state.program_hdr, GL_VERTEX_SHADER  , "main.vert");
  program_source(state.program_hdr, GL_FRAGMENT_SHADER, "main.frag");
  program_source(state.program_hdr, GL_GEOMETRY_SHADER, "main.geom");
  program_link  (state.program_hdr);

  state.program_tonemap = new_program();
  program_source(state.program_tonemap, GL_VERTEX_SHADER,  "deferred.vert");
  program_source(state.program_tonemap, GL_FRAGMENT_SHADER,"tonemap.frag" );
  program_link  (state.program_tonemap);

  state.program_blur = new_program();
  program_source(state.program_blur, GL_VERTEX_SHADER,  "deferred.vert");
  program_source(state.program_blur, GL_FRAGMENT_SHADER,"blur.frag"    );
  program_link  (state.program_blur);

  state.program_lum = new_program();
  program_source(state.program_lum, GL_VERTEX_SHADER  , "deferred.vert");
  program_source(state.program_lum, GL_FRAGMENT_SHADER, "luminance.frag");
  program_link  (state.program_lum);
}

void init_fbos(gl_state &state)
{
  int blur_dsc = 2;
  state.blur_downscale = blur_dsc;
  
  glCreateFramebuffers(4, state.fbos);
  glCreateTextures(GL_TEXTURE_2D, 4, state.attachs);

  int base_width  = state.width +2*FBO_MARGIN;
  int base_height = state.height+2*FBO_MARGIN;

  int widths [] = { base_width,
                    base_width/blur_dsc,
                    base_width/blur_dsc,
                    base_width/2};

  int heights[] = { base_height,
                    base_height/blur_dsc,
                    base_height/blur_dsc,
                    base_height/2};

  state.lum_lod = (int)floor(log2(max(base_width,base_height)/2));
  int mipmaps[] = { 1, 1, 1, state.lum_lod+1};
  GLenum types[] = {GL_RGBA16F, GL_RGBA16F, GL_RGBA16F, GL_R16F};
  GLenum min_filters[] = {GL_LINEAR, GL_LINEAR,GL_LINEAR,GL_LINEAR_MIPMAP_LINEAR};

  for (int i=0;i<4;++i)
  {
    glTextureStorage2D(state.attachs[i], mipmaps[i], types[i], widths[i], heights[i]);
    glTextureParameteri(state.attachs[i], GL_TEXTURE_MIN_FILTER, min_filters[i]);
    glTextureParameteri(state.attachs[i], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(state.attachs[i], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(state.fbos[i], GL_COLOR_ATTACHMENT0, state.attachs[i], 0);
  }
}

void set_uniforms(gl_state &state, sim_param params)
{
  // const Uniforms
  glProgramUniform1f(state.program_interaction, 0, params.dt);
  glProgramUniform1f(state.program_interaction, 1, params.G);
  glProgramUniform1f(state.program_interaction, 2, params.damping);
  glProgramUniform1f(state.program_integration, 0, params.dt);
  // NDC sprite size
  glProgramUniform2f(state.program_hdr, 8, 
    state.tex_size/float(2*state.width), 
    state.tex_size/float(2*state.height));
  // Blur sample offset length
  glProgramUniform2f(state.program_blur, 0, 
    (float)state.blur_downscale/state.width, 
    (float)state.blur_downscale/state.height);
}

void step_sim(gl_state &state, size_t num_particles)
{
  // Interaction step
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
  glUseProgram(state.program_interaction);
  glDispatchCompute(num_particles/256, 1, 1);

  // Integration step
  glUseProgram(state.program_integration);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
  glDispatchCompute(num_particles/256, 1, 1);
}

void render(gl_state &state, size_t num_particles, glm::mat4 proj_mat, glm::mat4 view_mat)
{
  // Particle HDR rendering
  glViewport(0,0,state.width+2*FBO_MARGIN, state.height+2*FBO_MARGIN);
  glBindVertexArray(state.vao_particles);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE);
  glBindFramebuffer(GL_FRAMEBUFFER, state.fbos[0]);
  glUseProgram(state.program_hdr);
  glClear(GL_COLOR_BUFFER_BIT);
  glProgramUniformMatrix4fv(state.program_hdr, 0, 1, GL_FALSE, glm::value_ptr(view_mat));
  glProgramUniformMatrix4fv(state.program_hdr, 4, 1, GL_FALSE, glm::value_ptr(proj_mat));
  glBindTextureUnit(0, state.flare_tex);
  glMemoryBarrier(GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
  glDrawArrays(GL_POINTS, 0, num_particles);

  glBindVertexArray(state.vao_deferred);
  glDisable(GL_BLEND);

  glViewport(0,0,
    (state.width +2*FBO_MARGIN)/state.blur_downscale, 
    (state.height+2*FBO_MARGIN)/state.blur_downscale);
  glUseProgram(state.program_blur);

  // Blur pingpong (N horizontal blurs then N vertical blurs)
  int loop = 0;
  for (int i=0;i<2;++i)
  {
    if (i==0) glProgramUniform2f(state.program_blur, 1, 1, 0);
    else      glProgramUniform2f(state.program_blur, 1, 0, 1);
    for (int j=0;j<100;++j)
    {
      GLuint fbo = state.fbos[(loop%2)+1];
      GLuint attach = state.attachs[loop?((loop+1)%2+1):0];
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glBindTextureUnit(0, attach);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      loop++;
    }
  }

  // Average luminance
  glViewport(0,0,
    (state.width +2*FBO_MARGIN)/2, 
    (state.height+2*FBO_MARGIN)/2);
  glBindFramebuffer(GL_FRAMEBUFFER, state.fbos[3]);
  glUseProgram(state.program_lum);
  glBindTextureUnit(0, state.attachs[0]);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glGenerateTextureMipmap(state.attachs[3]);

  // Tonemapping step (direct to screen)
  glViewport(0,0,state.width,state.height);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glUseProgram(state.program_tonemap);
  glProgramUniform1i(state.program_tonemap, 0, state.lum_lod);
  glBindTextureUnit(0, state.attachs[0]);
  glBindTextureUnit(1, state.attachs[2]);
  glBindTextureUnit(2, state.attachs[3]);
  glDrawArrays(GL_TRIANGLES, 0, 3);
}