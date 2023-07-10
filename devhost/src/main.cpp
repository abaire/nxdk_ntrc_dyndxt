#include <hal/debug.h>
#include <hal/video.h>
#include <pbkit/pbkit.h>
#include <windows.h>

#include <set>

#include "debug_output.h"
#include "nxdk_ext.h"
#include "pbkit_ext.h"
#include "renderer.h"
#include "tracelib/tracer_state_machine.h"
#include "xbdm.h"

#define ENABLE_TRACER_THREAD

static constexpr int kFramebufferWidth = 640;
static constexpr int kFramebufferHeight = 480;
static constexpr int kTextureWidth = 256;
static constexpr int kTextureHeight = 256;

//! Send nop commands, used as a mechanism to mark interesting things it the
//! pgraph log.
static void Mark(uint32_t num_nops = 8);
static void Initialize(Renderer& renderer);
static void CreateGeometry(Renderer& renderer);

static std::atomic_bool has_rendered_frame(false);

static std::atomic<TracerState> tracer_state;
static std::atomic_bool request_processed(false);

static void OnTracerStateChanged(TracerState new_state) {
  std::string state_name;
#define HANDLE_STATE(val) \
  case val:               \
    state_name = #val;    \
    break

  switch (new_state) {
    HANDLE_STATE(STATE_FATAL_NOT_IN_NEW_FRAME_STATE);
    HANDLE_STATE(STATE_FATAL_NOT_IN_STABLE_STATE);
    HANDLE_STATE(STATE_FATAL_DISCARDING_FAILED);
    HANDLE_STATE(STATE_FATAL_PROCESS_PUSH_BUFFER_COMMAND_FAILED);
    HANDLE_STATE(STATE_SHUTDOWN_REQUESTED);
    HANDLE_STATE(STATE_SHUTDOWN);
    HANDLE_STATE(STATE_UNINITIALIZED);
    HANDLE_STATE(STATE_INITIALIZING);
    HANDLE_STATE(STATE_INITIALIZED);
    HANDLE_STATE(STATE_IDLE);
    HANDLE_STATE(STATE_IDLE_STABLE_PUSH_BUFFER);
    HANDLE_STATE(STATE_IDLE_NEW_FRAME);
    HANDLE_STATE(STATE_IDLE_LAST);
    HANDLE_STATE(STATE_WAITING_FOR_STABLE_PUSH_BUFFER);
    HANDLE_STATE(STATE_DISCARDING_UNTIL_FLIP);
    HANDLE_STATE(STATE_TRACING_UNTIL_FLIP);
    default:
      state_name = "<<UNKNOWN>>";
      break;
  }

  PrintMsg("Tracer state changed: %s[%d]", state_name.c_str(), new_state);
  tracer_state = new_state;
}

static void OnRequestProcessed() { request_processed = true; }

static uint8_t discard_buffer[4096];
static void OnPGRAPHBytesAvailable(uint32_t bytes_written) {
  PrintMsg("New PGRAPH bytes available: %u", bytes_written);
  TracerLockPGRAPHBuffer();
  while (TracerReadPGRAPHBuffer(discard_buffer, sizeof(discard_buffer))) {
  }
  TracerUnlockPGRAPHBuffer();
}

static void OnAuxBytesAvailable(uint32_t bytes_written) {
  PrintMsg("New aux bytes available: %u", bytes_written);
  TracerLockAuxBuffer();
  while (TracerReadAuxBuffer(discard_buffer, sizeof(discard_buffer))) {
  }
  TracerUnlockAuxBuffer();
}

static void WaitForState(TracerState state) {
  while (tracer_state != state) {
    Sleep(1);
  }
}

static void WaitForState(const std::set<TracerState>& states) {
  while (states.find(tracer_state) == states.cend()) {
    Sleep(1);
  }
}

static void WaitForRequestComplete() {
  while (TracerIsProcessingRequest()) {
    Sleep(1);
  }
}

#ifdef ENABLE_TRACER_THREAD
static DWORD __attribute__((stdcall))
TracerThreadMain(LPVOID lpThreadParameter) {
  while (!has_rendered_frame) {
    Sleep(1);
  }

  auto init_result =
      TracerInitialize(OnTracerStateChanged, OnRequestProcessed,
                       OnPGRAPHBytesAvailable, OnAuxBytesAvailable);
  if (!XBOX_SUCCESS(init_result)) {
    PrintMsg("Failed to initialize tracer: 0x%X", init_result);
    return init_result;
  }

  // Create a tracer instance and wait for it to stabilize.
  TracerConfig config;
  TracerGetDefaultConfig(&config);
  auto create_result = TracerCreate(&config);
  if (!XBOX_SUCCESS(create_result)) {
    PrintMsg("Failed to create tracer: 0x%X", create_result);
    return init_result;
  }
  WaitForState(STATE_IDLE);

  //  TracerCreate
  PrintMsg("About to start wait for stable pbuffer state...");
  request_processed = false;
  if (!TracerBeginWaitForStablePushBufferState()) {
    PrintMsg("TracerBeginWaitForStablePushBufferState failed!");
    TracerShutdown();
    return 1;
  } else {
    WaitForRequestComplete();
  }
  PrintMsg("Achieved stable pbuffer state...");

  PrintMsg("About to discard until next frame flip...");
  request_processed = false;
  if (!TracerBeginDiscardUntilFlip(TRUE)) {
    PrintMsg("TracerBeginDiscardUntilFlip failed!");
    TracerShutdown();
    return 1;
  } else {
    WaitForRequestComplete();
  }
  PrintMsg("New frame started!");

  request_processed = false;
  if (!TracerTraceCurrentFrame()) {
    PrintMsg("TracerTraceCurrentFrame failed!");
    TracerShutdown();
    return 1;
  } else {
    WaitForRequestComplete();
  }

  TracerShutdown();
  return 0;
}
#endif  // ENABLE_TRACER_THREAD

static float wrap_color(float val) {
  while (val < 0.f) {
    val += 1.f;
  }
  while (val > 1.f) {
    val -= 1.f;
  }
  return val;
}

int main() {
  XVideoSetMode(kFramebufferWidth, kFramebufferHeight, 32, REFRESH_DEFAULT);

  int status = pb_init();
  if (status) {
    debugPrint("pb_init Error %d\n", status);
    pb_show_debug_screen();
    Sleep(2000);
    return 1;
  }

  pb_show_front_screen();

  auto renderer = Renderer(kFramebufferWidth, kFramebufferHeight, kTextureWidth,
                           kTextureHeight);

  CreateGeometry(renderer);

#ifdef ENABLE_TRACER_THREAD
  DWORD tracer_thread_id;
  auto tracer_thread =
      CreateThread(nullptr, 0, TracerThreadMain, nullptr, 0, &tracer_thread_id);
  if (!tracer_thread) {
    debugPrint("Failed to create tracer thread.\n");
    pb_show_debug_screen();
    Sleep(2000);
    return 1;
  }
#endif  // ENABLE_TRACER_THREAD

  // Render some test content.
  // Note that this is intentionally inefficient, the intent is to test the
  // pgraph tracer, so there is more frequent interaction with the pushbuffer
  // than necessary.

  float r = 1.0f;
  float g = 0.25f;
  float b = 0.33f;
  while (true) {
    Initialize(renderer);

    MATRIX matrix;
    matrix_unit(matrix);
    renderer.SetFixedFunctionModelViewMatrix(matrix);
    renderer.SetFixedFunctionProjectionMatrix(matrix);

    renderer.PrepareDraw(0xFF333333);

    auto p = pb_begin();

    // Set up a directional light.
    p = pb_push1(p, NV097_SET_LIGHT_ENABLE_MASK,
                 NV097_SET_LIGHT_ENABLE_MASK_LIGHT0_INFINITE);

    // Ambient color comes from the material's diffuse color.
    p = pb_push3(p, NV097_SET_LIGHT_AMBIENT_COLOR, 0, 0, 0);
    p = pb_push3f(p, NV097_SET_LIGHT_DIFFUSE_COLOR, r, g, b);
    p = pb_push3f(p, NV097_SET_LIGHT_SPECULAR_COLOR, 0.f, 0.f, 0.f);
    p = pb_push1(p, NV097_SET_LIGHT_LOCAL_RANGE, 0x7149f2ca);  // 1e+30
    p = pb_push3(p, NV097_SET_LIGHT_INFINITE_HALF_VECTOR, 0, 0, 0);
    p = pb_push3f(p, NV097_SET_LIGHT_INFINITE_DIRECTION, 0.0f, 0.0f, 1.0f);

    uint32_t control0 =
        MASK(NV097_SET_CONTROL0_Z_FORMAT, NV097_SET_CONTROL0_Z_FORMAT_FIXED);
    p = pb_push1(p, NV097_SET_CONTROL0, control0);

    p = pb_push1(p, NV097_SET_VERTEX_DATA4UB + 0x0C, 0xFFFFFFFF);
    p = pb_push1(p, NV097_SET_VERTEX_DATA4UB + 0x10, 0);
    p = pb_push1(p, NV097_SET_VERTEX_DATA4UB + 0x1C, 0xFFFFFFFF);
    p = pb_push1(p, NV097_SET_VERTEX_DATA4UB + 0x20, 0);

    p = pb_push1(p, NV10_TCL_PRIMITIVE_3D_POINT_PARAMETERS_ENABLE, 0x0);

    p = pb_push1(p, NV097_SET_SPECULAR_PARAMS, 0xBF7730E0);
    p = pb_push1(p, NV097_SET_SPECULAR_PARAMS + 4, 0xC0497B30);
    p = pb_push1(p, NV097_SET_SPECULAR_PARAMS + 8, 0x404BAEF8);
    p = pb_push1(p, NV097_SET_SPECULAR_PARAMS + 12, 0xBF6E9EE4);
    p = pb_push1(p, NV097_SET_SPECULAR_PARAMS + 16, 0xC0463F88);
    p = pb_push1(p, NV097_SET_SPECULAR_PARAMS + 20, 0x404A97CF);

    p = pb_push1(p, NV097_SET_LIGHT_CONTROL, 0x10001);

    p = pb_push1(p, NV097_SET_LIGHTING_ENABLE, 0x1);
    p = pb_push1(p, NV097_SET_SPECULAR_ENABLE, 0x1);

    p = pb_push1(p, NV097_SET_COLOR_MATERIAL,
                 NV097_SET_COLOR_MATERIAL_DIFFUSE_FROM_MATERIAL);
    p = pb_push3(p, NV097_SET_SCENE_AMBIENT_COLOR, 0x0, 0x3C6DDACA, 0x0);

    p = pb_push1(p, NV097_SET_MATERIAL_EMISSION, 0x0);
    p = pb_push1(p, NV097_SET_MATERIAL_EMISSION + 4, 0x0);
    p = pb_push1(p, NV097_SET_MATERIAL_EMISSION + 8, 0x0);

    float material_alpha = 0.75f;
    uint32_t alpha_int = *(uint32_t*)&material_alpha;
    p = pb_push1(p, NV097_SET_MATERIAL_ALPHA, alpha_int);

    pb_end(p);

    renderer.DrawArrays(renderer.POSITION | renderer.NORMAL | renderer.DIFFUSE |
                        renderer.SPECULAR);

    Mark();

    renderer.FinishDraw();

    has_rendered_frame = true;

    r = wrap_color(r + 0.001f);
    g = wrap_color(g - 0.005f);
    b = wrap_color(b + 0.005f);
  }

  return 0;
}

static void Mark(uint32_t num_nops) {
  auto p = pb_begin();
  for (auto i = 0; i < num_nops; ++i) {
    p = pb_push1(p, NV097_NO_OPERATION, 0);
  }
  pb_end(p);
}

static void Initialize(Renderer& renderer) {
  const uint32_t kFramebufferPitch = renderer.GetFramebufferWidth() * 4;
  renderer.SetSurfaceFormat(Renderer::SCF_A8R8G8B8, Renderer::SZF_Z16,
                            renderer.GetFramebufferWidth(),
                            renderer.GetFramebufferHeight());

  auto p = pb_begin();
  p = pb_push1(p, NV097_SET_SURFACE_PITCH,
               SET_MASK(NV097_SET_SURFACE_PITCH_COLOR, kFramebufferPitch) |
                   SET_MASK(NV097_SET_SURFACE_PITCH_ZETA, kFramebufferPitch));
  p = pb_push1(p, NV097_SET_SURFACE_CLIP_HORIZONTAL,
               renderer.GetFramebufferWidth() << 16);
  p = pb_push1(p, NV097_SET_SURFACE_CLIP_VERTICAL,
               renderer.GetFramebufferHeight() << 16);

  p = pb_push1(p, NV097_SET_LIGHTING_ENABLE, false);
  p = pb_push1(p, NV097_SET_SPECULAR_ENABLE, false);
  p = pb_push1(p, NV097_SET_LIGHT_CONTROL, 0x20001);
  p = pb_push1(p, NV097_SET_LIGHT_ENABLE_MASK,
               NV097_SET_LIGHT_ENABLE_MASK_LIGHT0_OFF);
  p = pb_push1(p, NV097_SET_COLOR_MATERIAL,
               NV097_SET_COLOR_MATERIAL_ALL_FROM_MATERIAL);
  p = pb_push1f(p, NV097_SET_MATERIAL_ALPHA, 1.0f);

  p = pb_push1(p, NV20_TCL_PRIMITIVE_3D_LIGHT_MODEL_TWO_SIDE_ENABLE, 0);
  p = pb_push1(p, NV097_SET_FRONT_POLYGON_MODE,
               NV097_SET_FRONT_POLYGON_MODE_V_FILL);
  p = pb_push1(p, NV097_SET_BACK_POLYGON_MODE,
               NV097_SET_FRONT_POLYGON_MODE_V_FILL);

  p = pb_push1(p, NV097_SET_VERTEX_DATA4UB + 0x10, 0);           // Specular
  p = pb_push1(p, NV097_SET_VERTEX_DATA4UB + 0x1C, 0xFFFFFFFF);  // Back diffuse
  p = pb_push1(p, NV097_SET_VERTEX_DATA4UB + 0x20, 0);  // Back specular

  p = pb_push1(p, NV097_SET_POINT_PARAMS_ENABLE, false);
  p = pb_push1(p, NV097_SET_POINT_SMOOTH_ENABLE, false);
  p = pb_push1(p, NV097_SET_POINT_SIZE, 8);

  p = pb_push1(p, NV097_SET_DOT_RGBMAPPING, 0);

  p = pb_push1(p, NV097_SET_SHADE_MODEL, NV097_SET_SHADE_MODEL_SMOOTH);
  pb_end(p);

  Renderer::SetWindowClipExclusive(false);
  // Note, setting the first clip region will cause the hardware to also set all
  // subsequent regions.
  Renderer::SetWindowClip(renderer.GetFramebufferWidth(),
                          renderer.GetFramebufferHeight());

  renderer.SetBlend();

  renderer.ClearInputColorCombiners();
  renderer.ClearInputAlphaCombiners();
  renderer.ClearOutputColorCombiners();
  renderer.ClearOutputAlphaCombiners();

  renderer.SetCombinerControl(1);
  renderer.SetInputColorCombiner(
      0, Renderer::SRC_DIFFUSE, false, Renderer::MAP_UNSIGNED_IDENTITY,
      Renderer::SRC_ZERO, false, Renderer::MAP_UNSIGNED_INVERT);
  renderer.SetInputAlphaCombiner(
      0, Renderer::SRC_DIFFUSE, true, Renderer::MAP_UNSIGNED_IDENTITY,
      Renderer::SRC_ZERO, false, Renderer::MAP_UNSIGNED_INVERT);

  renderer.SetOutputColorCombiner(0, Renderer::DST_DISCARD,
                                  Renderer::DST_DISCARD, Renderer::DST_R0);
  renderer.SetOutputAlphaCombiner(0, Renderer::DST_DISCARD,
                                  Renderer::DST_DISCARD, Renderer::DST_R0);

  renderer.SetFinalCombiner0(
      Renderer::SRC_ZERO, false, false, Renderer::SRC_ZERO, false, false,
      Renderer::SRC_ZERO, false, false, Renderer::SRC_R0);
  renderer.SetFinalCombiner1(Renderer::SRC_ZERO, false, false,
                             Renderer::SRC_ZERO, false, false, Renderer::SRC_R0,
                             true, false, false, false, true);

  renderer.SetShaderStageProgram(Renderer::STAGE_NONE, Renderer::STAGE_NONE,
                                 Renderer::STAGE_NONE, Renderer::STAGE_NONE);

  while (pb_busy()) {
    /* Wait for completion... */
  }

  p = pb_begin();

  MATRIX identity_matrix;
  matrix_unit(identity_matrix);
  for (auto i = 0; i < 4; ++i) {
    auto& stage = renderer.GetTextureStage(i);
    stage.SetUWrap(TextureStage::WRAP_CLAMP_TO_EDGE, false);
    stage.SetVWrap(TextureStage::WRAP_CLAMP_TO_EDGE, false);
    stage.SetPWrap(TextureStage::WRAP_CLAMP_TO_EDGE, false);
    stage.SetQWrap(false);

    stage.SetEnabled(false);
    stage.SetCubemapEnable(false);
    stage.SetFilter();
    stage.SetAlphaKillEnable(false);
    stage.SetLODClamp(0, 4095);

    stage.SetTextureMatrixEnable(false);
    stage.SetTextureMatrix(identity_matrix);

    stage.SetTexgenS(TextureStage::TG_DISABLE);
    stage.SetTexgenT(TextureStage::TG_DISABLE);
    stage.SetTexgenR(TextureStage::TG_DISABLE);
    stage.SetTexgenQ(TextureStage::TG_DISABLE);
  }

  // TODO: Set up with TextureStage instances in renderer.
  {
    uint32_t address = NV097_SET_TEXTURE_ADDRESS;
    uint32_t control = NV097_SET_TEXTURE_CONTROL0;
    uint32_t filter = NV097_SET_TEXTURE_FILTER;
    p = pb_push1(p, address, 0x10101);
    p = pb_push1(p, control, 0x3ffc0);
    p = pb_push1(p, filter, 0x1012000);

    address += 0x40;
    control += 0x40;
    filter += 0x40;
    p = pb_push1(p, address, 0x10101);
    p = pb_push1(p, control, 0x3ffc0);
    p = pb_push1(p, filter, 0x1012000);

    address += 0x40;
    control += 0x40;
    filter += 0x40;
    p = pb_push1(p, address, 0x10101);
    p = pb_push1(p, control, 0x3ffc0);
    p = pb_push1(p, filter, 0x1012000);

    address += 0x40;
    control += 0x40;
    filter += 0x40;
    p = pb_push1(p, address, 0x10101);
    p = pb_push1(p, control, 0x3ffc0);
    p = pb_push1(p, filter, 0x1012000);
  }

  p = pb_push1(p, NV097_SET_FOG_ENABLE, false);
  p = pb_push4(p, NV097_SET_TEXTURE_MATRIX_ENABLE, 0, 0, 0, 0);

  p = pb_push1(p, NV097_SET_FRONT_FACE, NV097_SET_FRONT_FACE_V_CW);
  p = pb_push1(p, NV097_SET_CULL_FACE, NV097_SET_CULL_FACE_V_BACK);
  p = pb_push1(p, NV097_SET_CULL_FACE_ENABLE, true);

  p = pb_push1(p, NV097_SET_COLOR_MASK,
               NV097_SET_COLOR_MASK_BLUE_WRITE_ENABLE |
                   NV097_SET_COLOR_MASK_GREEN_WRITE_ENABLE |
                   NV097_SET_COLOR_MASK_RED_WRITE_ENABLE |
                   NV097_SET_COLOR_MASK_ALPHA_WRITE_ENABLE);

  p = pb_push1(p, NV097_SET_DEPTH_TEST_ENABLE, false);
  p = pb_push1(p, NV097_SET_DEPTH_MASK, true);
  p = pb_push1(p, NV097_SET_DEPTH_FUNC, NV097_SET_DEPTH_FUNC_V_LESS);
  p = pb_push1(p, NV097_SET_STENCIL_TEST_ENABLE, false);
  p = pb_push1(p, NV097_SET_STENCIL_MASK, true);

  p = pb_push1(p, NV097_SET_NORMALIZATION_ENABLE, false);
  pb_end(p);

  renderer.SetDefaultViewportAndFixedFunctionMatrices();
  renderer.SetDepthBufferFloatMode(false);

  renderer.SetVertexShaderProgram(nullptr);

  const TextureFormatInfo& texture_format =
      GetTextureFormatInfo(NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X8R8G8B8);
  renderer.SetTextureFormat(texture_format, 0);
  renderer.SetDefaultTextureParams(0);
  renderer.SetTextureFormat(texture_format, 1);
  renderer.SetDefaultTextureParams(1);
  renderer.SetTextureFormat(texture_format, 2);
  renderer.SetDefaultTextureParams(2);
  renderer.SetTextureFormat(texture_format, 3);
  renderer.SetDefaultTextureParams(3);

  renderer.SetTextureStageEnabled(0, false);
  renderer.SetTextureStageEnabled(1, false);
  renderer.SetTextureStageEnabled(2, false);
  renderer.SetTextureStageEnabled(3, false);
  renderer.SetShaderStageProgram(Renderer::STAGE_NONE);
  renderer.SetShaderStageInput(0, 0);

  p = pb_begin();
  p = pb_push1(p, NV097_SET_SHADER_STAGE_PROGRAM, 0);
  pb_end(p);
}

static void CreateGeometry(Renderer& renderer) {
  auto fb_width = static_cast<float>(renderer.GetFramebufferWidth());
  auto fb_height = static_cast<float>(renderer.GetFramebufferHeight());

  float left = -1.0f * floorf(fb_width / 4.0f);
  float right = floorf(fb_width / 4.0f);
  float top = -1.0f * floorf(fb_height / 3.0f);
  float bottom = floorf(fb_height / 3.0f);
  float mid_width = left + (right - left) * 0.5f;

  uint32_t num_quads = 2;
  std::shared_ptr<VertexBuffer> buffer =
      renderer.AllocateVertexBuffer(6 * num_quads);

  Color ul{0.4f, 0.1f, 0.1f, 0.25f};
  Color ll{0.0f, 1.0f, 0.0f, 1.0f};
  Color lr{0.0f, 0.0f, 1.0f, 1.0f};
  Color ur{0.5f, 0.5f, 0.5f, 1.0f};
  Color ul_s{0.0f, 1.0f, 0.0f, 0.5f};
  Color ll_s{1.0f, 0.0f, 0.0f, 0.1f};
  Color lr_s{1.0f, 1.0f, 0.0f, 0.5f};
  Color ur_s{0.0f, 1.0f, 1.0f, 0.75f};

  float z = 10.0f;
  buffer->DefineBiTri(0, left + 10, top + 4, mid_width + 10, bottom - 10, z, z,
                      z, z, ul, ll, lr, ur, ul_s, ll_s, lr_s, ur_s);
  // Point normals for half the quad away from the camera.
  Vertex* v = buffer->Lock();
  v[0].normal[2] = -1.0f;
  v[1].normal[2] = -1.0f;
  v[2].normal[2] = -1.0f;
  buffer->Unlock();

  ul.SetRGBA(1.0f, 1.0f, 0.0f, 1.0f);
  ul_s.SetRGBA(1.0f, 0.0f, 0.0f, 0.25f);

  ll.SetGreyA(0.5, 1.0f);
  ll_s.SetRGBA(0.3f, 0.3f, 1.0f, 1.0f);

  ur.SetRGBA(0.0f, 0.3f, 0.8f, 0.15f);
  ur_s.SetRGBA(0.9f, 0.9f, 0.4f, 0.33);

  lr.SetRGBA(1.0f, 0.0f, 0.0f, 0.75f);
  lr_s.SetRGBA(0.95f, 0.5f, 0.8f, 0.05);
  z = 9.75f;
  buffer->DefineBiTri(1, mid_width - 10, top + 4, right - 10, bottom - 10, z, z,
                      z, z, ul, ll, lr, ur, ul_s, ll_s, lr_s, ur_s);
}
