#include "stdafx.h"

#pragma warning(push)
#pragma warning(disable : 4995)
#include <d3dx/d3dx9.h>
#pragma warning(pop)

#include "ResourceManager.h"
#include "blenders\Blender_Recorder.h"
#include "blenders\Blender.h"

#include "../../xr_3da/igame_persistent.h"
#include "../../xr_3da/environment.h"

#include "dxRenderDeviceRender.h"

// matrices
#define BIND_DECLARE(xf) \
    static class cl_xform_##xf final : public R_constant_setup \
    { \
        void setup(R_constant* C) override { RCache.xforms.set_c_##xf(C); } \
    } binder_##xf
BIND_DECLARE(w);
BIND_DECLARE(invw);
BIND_DECLARE(v);
BIND_DECLARE(p);
BIND_DECLARE(wv);
BIND_DECLARE(vp);
BIND_DECLARE(wvp);

#define DECLARE_TREE_BIND(c) \
    static class cl_tree_##c final : public R_constant_setup \
    { \
        void setup(R_constant* C) override { RCache.tree.set_c_##c(C); } \
    } tree_binder_##c
DECLARE_TREE_BIND(m_xform_v);
DECLARE_TREE_BIND(m_xform);
DECLARE_TREE_BIND(consts);
DECLARE_TREE_BIND(wave);
DECLARE_TREE_BIND(wind);
DECLARE_TREE_BIND(c_scale);
DECLARE_TREE_BIND(c_bias);
DECLARE_TREE_BIND(c_sun);

static class cl_hemi_cube_pos_faces final : public R_constant_setup
{
    void setup(R_constant* C) override { RCache.hemi.set_c_pos_faces(C); }
} binder_hemi_cube_pos_faces;

static class cl_hemi_cube_neg_faces final : public R_constant_setup
{
    void setup(R_constant* C) override { RCache.hemi.set_c_neg_faces(C); }
} binder_hemi_cube_neg_faces;

static class cl_material final : public R_constant_setup
{
    void setup(R_constant* C) override { RCache.hemi.set_c_material(C); }
} binder_material;

static class cl_texgen final : public R_constant_setup
{
    void setup(R_constant* C) override
    {
        Fmatrix mTexgen;

#if defined(USE_DX10) || defined(USE_DX11)
        Fmatrix mTexelAdjust = {0.5f, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f, 0.0f, 1.0f};
#else //	USE_DX10
        float _w = float(RDEVICE.dwWidth);
        float _h = float(RDEVICE.dwHeight);
        float o_w = (.5f / _w);
        float o_h = (.5f / _h);
        Fmatrix mTexelAdjust = {0.5f, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f + o_w, 0.5f + o_h, 0.0f, 1.0f};
#endif //	USE_DX10

        mTexgen.mul(mTexelAdjust, RCache.xforms.m_wvp);

        RCache.set_c(C, mTexgen);
    }
} binder_texgen;

static class cl_VPtexgen final : public R_constant_setup
{
    void setup(R_constant* C) override
    {
        Fmatrix mTexgen;

#if defined(USE_DX10) || defined(USE_DX11)
        Fmatrix mTexelAdjust = {0.5f, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f, 0.0f, 1.0f};
#else //	USE_DX10
        float _w = float(RDEVICE.dwWidth);
        float _h = float(RDEVICE.dwHeight);
        float o_w = (.5f / _w);
        float o_h = (.5f / _h);
        Fmatrix mTexelAdjust = {0.5f, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f + o_w, 0.5f + o_h, 0.0f, 1.0f};
#endif //	USE_DX10

        mTexgen.mul(mTexelAdjust, RCache.xforms.m_vp);

        RCache.set_c(C, mTexgen);
    }
} binder_VPtexgen;

// fog
static class cl_fog_plane final : public R_constant_setup
{
    Fvector4 result;
    void setup(R_constant* C) override
    {
        // Plane
        Fvector4 plane;
        Fmatrix& M = Device.mFullTransform;
        plane.x = -(M._14 + M._13);
        plane.y = -(M._24 + M._23);
        plane.z = -(M._34 + M._33);
        plane.w = -(M._44 + M._43);
        float denom = -1.0f / _sqrt(_sqr(plane.x) + _sqr(plane.y) + _sqr(plane.z));
        plane.mul(denom);

        // Near/Far
        float A = g_pGamePersistent->Environment().CurrentEnv->fog_near;
        float B = 1 / (g_pGamePersistent->Environment().CurrentEnv->fog_far - A);
        result.set(-plane.x * B, -plane.y * B, -plane.z * B, 1 - (plane.w - A) * B); // view-plane

        RCache.set_c(C, result);
    }
} binder_fog_plane;

// fog-params
static class cl_fog_params final : public R_constant_setup
{
    Fvector4 result;
    void setup(R_constant* C) override
    {
        // Near/Far
        float n = g_pGamePersistent->Environment().CurrentEnv->fog_near;
        float f = g_pGamePersistent->Environment().CurrentEnv->fog_far;
        float r = 1 / (f - n);
        result.set(-n * r, r, r, r);

        RCache.set_c(C, result);
    }
} binder_fog_params;

// fog-color
static class cl_fog_color final : public R_constant_setup
{
    Fvector4 result;
    void setup(R_constant* C) override
    {
        CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
        result.set(desc.fog_color.x, desc.fog_color.y, desc.fog_color.z, 0);

        RCache.set_c(C, result);
    }
} binder_fog_color;

// times
static class cl_times final : public R_constant_setup
{
    void setup(R_constant* C) override
    {
        float t = RDEVICE.fTimeGlobal;
        RCache.set_c(C, t, t * 10, t / 10, _sin(t));
    }
} binder_times;

// eye-params
static class cl_eye_P final : public R_constant_setup
{
    void setup(R_constant* C) override
    {
        Fvector& V = RDEVICE.vCameraPosition;
        RCache.set_c(C, V.x, V.y, V.z, 1);
    }
} binder_eye_P;

// eye-params
static class cl_eye_D final : public R_constant_setup
{
    void setup(R_constant* C) override
    {
        Fvector& V = RDEVICE.vCameraDirection;
        RCache.set_c(C, V.x, V.y, V.z, 0);
    }
} binder_eye_D;


// interpolated eye position (crookr scope parallax)
     // We can improve this by clamping the magnitude of the travel here instead of in-shader.
     // it would fix the issue with the fog "sticking" when moving too far off center
extern float scope_fog_interp;
extern float scope_fog_travel;
 class cl_eye_PL:public R_constant_setup 
{
     Fvector tV;
     virtual void setup(R_constant* C) 
    {
        Fvector& V = RDEVICE.vCameraPosition;
        tV = tV.lerp(tV, V, scope_fog_interp);
        RCache.set_c(C, tV.x, tV.y, tV.z, 1);
        
    }
};
 static cl_eye_PL binder_eye_PL;

 // interpolated eye direction (crookr scope parallax)
     class cl_eye_DL:public R_constant_setup
 {
     Fvector tV;
     virtual void setup(R_constant* C) 
     {
         Fvector& V = RDEVICE.vCameraDirection;
         tV = tV.lerp(tV, V, scope_fog_interp);
         RCache.set_c(C, tV.x, tV.y, tV.z, 0);
         
     }
     
 };
static cl_eye_DL binder_eye_DL;

 // fake scope params (crookr)
     extern float scope_outerblur;
 extern float scope_innerblur;
extern float scope_scrollpower;
 extern float scope_brightness;
 class cl_fakescope_params:public R_constant_setup 
{
     virtual void setup(R_constant* C) 
    {
        RCache.set_c(C, scope_scrollpower, scope_innerblur, scope_outerblur, scope_brightness);
        
    }
    
};
static cl_fakescope_params binder_fakescope_params;

extern float scope_ca;
extern float scope_fog_attack;
extern float scope_fog_mattack;
// extern float scope_fog_travel;
class cl_fakescope_ca : public R_constant_setup
{
    virtual void setup(R_constant* C) { RCache.set_c(C, scope_ca, scope_fog_attack, scope_fog_mattack, scope_fog_travel); }
};
static cl_fakescope_ca binder_fakescope_ca;

 extern float scope_radius;
 extern float scope_fog_radius;
 extern float scope_fog_sharp;
 // extern float scope_drift_amount;
     class cl_fakescope_params3:public R_constant_setup 
{
    virtual void setup(R_constant* C)
    {
        RCache.set_c(C, scope_radius, scope_fog_radius, scope_fog_sharp, 0.0f);
        
    }
   
};
static cl_fakescope_params3 binder_fakescope_params3;


// eye-params
static class cl_eye_N final : public R_constant_setup
{
    void setup(R_constant* C) override
    {
        Fvector& V = RDEVICE.vCameraTop;
        RCache.set_c(C, V.x, V.y, V.z, 0);
    }
} binder_eye_N;

// D-Light0
static class cl_sun0_color final : public R_constant_setup
{
    Fvector4 result;
    void setup(R_constant* C) override
    {
        CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
        result.set(desc.sun_color.x, desc.sun_color.y, desc.sun_color.z, 0);

        RCache.set_c(C, result);
    }
} binder_sun0_color;

static class cl_sun0_dir_w final : public R_constant_setup
{
    Fvector4 result;
    void setup(R_constant* C) override
    {
        CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
        result.set(desc.sun_dir.x, desc.sun_dir.y, desc.sun_dir.z, 0);

        RCache.set_c(C, result);
    }
} binder_sun0_dir_w;

static class cl_sun0_dir_e final : public R_constant_setup
{
    Fvector4 result;
    void setup(R_constant* C) override
    {
        Fvector D;
        CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
        Device.mView.transform_dir(D, desc.sun_dir);
        D.normalize();
        result.set(D.x, D.y, D.z, 0);

        RCache.set_c(C, result);
    }
} binder_sun0_dir_e;

static class cl_amb_color final : public R_constant_setup
{
    Fvector4 result;
    void setup(R_constant* C) override
    {
        CEnvDescriptorMixer& desc = *g_pGamePersistent->Environment().CurrentEnv;
        result.set(desc.ambient.x, desc.ambient.y, desc.ambient.z, desc.weight);

        RCache.set_c(C, result);
    }
} binder_amb_color;

static class cl_hemi_color final : public R_constant_setup
{
    Fvector4 result;
    void setup(R_constant* C) override
    {
        CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
        result.set(desc.hemi_color.x, desc.hemi_color.y, desc.hemi_color.z, desc.hemi_color.w);

        RCache.set_c(C, result);
    }
} binder_hemi_color;

static class cl_screen_res final : public R_constant_setup
{
    void setup(R_constant* C) override { RCache.set_c(C, (float)RDEVICE.dwWidth, (float)RDEVICE.dwHeight, 1.0f / (float)RDEVICE.dwWidth, 1.0f / (float)RDEVICE.dwHeight); }
} binder_screen_res;

static class cl_screen_params final : public R_constant_setup
{
    Fvector4 result;
    void setup(R_constant* C) override
    {
        result.set(Device.fFOV, Device.fASPECT, tan(deg2rad(Device.fFOV) / 2), g_pGamePersistent->Environment().CurrentEnv->far_plane * 0.75f);

        RCache.set_c(C, result);
    }
} binder_screen_params;

float ps_r2_puddles_wetness = 0.f;
// да, -8 байт на херню
int puddles_c1 = 0;
int puddles_c2 = 0;

static class cl_rain_params final : public R_constant_setup
{
    void updcounter() {
        if (puddles_c1 < 2)
            puddles_c1++;
        else
        {
            puddles_c1 = 0;
            puddles_c2++;
        }
        if (puddles_c2 > 25)
            puddles_c2 = 0;
    }

    void setup(R_constant* C) override { 
        float rain_density = g_pGamePersistent->Environment().CurrentEnv->rain_density;
        if (puddles_c2 == 25)
        {
            if (rain_density != 0 && ps_r2_puddles_wetness < 1)
                ps_r2_puddles_wetness += Device.fTimeDelta / 10;
            else
            {
                if (ps_r2_puddles_wetness > 0)
                    ps_r2_puddles_wetness -= Device.fTimeDelta / 10;
            }
        }

        RCache.set_c(C, rain_density, ps_r2_puddles_wetness, 0.0f, 0.0f);

        updcounter();
    }
} binder_rain_params;

static class cl_artifacts final : public R_constant_setup
{
    Fmatrix result{};

    void setup(R_constant* C) override
    {
        result._11 = shader_exports.get_artefact_position(start_val).x;
        result._12 = shader_exports.get_artefact_position(start_val).y;
        result._13 = shader_exports.get_artefact_position(start_val + 1).x;
        result._14 = shader_exports.get_artefact_position(start_val + 1).y;
        result._21 = shader_exports.get_artefact_position(start_val + 2).x;
        result._22 = shader_exports.get_artefact_position(start_val + 2).y;
        result._23 = shader_exports.get_artefact_position(start_val + 3).x;
        result._24 = shader_exports.get_artefact_position(start_val + 3).y;
        result._31 = shader_exports.get_artefact_position(start_val + 4).x;
        result._32 = shader_exports.get_artefact_position(start_val + 4).y;
        result._33 = shader_exports.get_artefact_position(start_val + 5).x;
        result._34 = shader_exports.get_artefact_position(start_val + 5).y;
        result._41 = shader_exports.get_artefact_position(start_val + 6).x;
        result._42 = shader_exports.get_artefact_position(start_val + 6).y;
        result._43 = shader_exports.get_artefact_position(start_val + 7).x;
        result._44 = shader_exports.get_artefact_position(start_val + 7).y;

        RCache.set_c(C, result);
    }

    u32 start_val;

public:
    cl_artifacts() = delete;
    cl_artifacts(u32 v) : start_val(v) {}
} binder_artifacts{0}, binder_artifacts2{8}, binder_artifacts3{16};

static class cl_anomalys final : public R_constant_setup
{
    Fmatrix result{};

    void setup(R_constant* C) override
    {
        result._11 = shader_exports.get_anomaly_position(start_val).x;
        result._12 = shader_exports.get_anomaly_position(start_val).y;
        result._13 = shader_exports.get_anomaly_position(start_val + 1).x;
        result._14 = shader_exports.get_anomaly_position(start_val + 1).y;
        result._21 = shader_exports.get_anomaly_position(start_val + 2).x;
        result._22 = shader_exports.get_anomaly_position(start_val + 2).y;
        result._23 = shader_exports.get_anomaly_position(start_val + 3).x;
        result._24 = shader_exports.get_anomaly_position(start_val + 3).y;
        result._31 = shader_exports.get_anomaly_position(start_val + 4).x;
        result._32 = shader_exports.get_anomaly_position(start_val + 4).y;
        result._33 = shader_exports.get_anomaly_position(start_val + 5).x;
        result._34 = shader_exports.get_anomaly_position(start_val + 5).y;
        result._41 = shader_exports.get_anomaly_position(start_val + 6).x;
        result._42 = shader_exports.get_anomaly_position(start_val + 6).y;
        result._43 = shader_exports.get_anomaly_position(start_val + 7).x;
        result._44 = shader_exports.get_anomaly_position(start_val + 7).y;

        RCache.set_c(C, result);
    }

    u32 start_val;

public:
    cl_anomalys() = delete;
    cl_anomalys(u32 v) : start_val(v) {}
} binder_anomalys{0}, binder_anomalys2{8}, binder_anomalys3{16};

static class cl_detector final : public R_constant_setup
{
    Fvector4 result;
    void setup(R_constant* C) override
    {
        result.set((float)(shader_exports.get_detector_params().x), (float)(shader_exports.get_detector_params().y), 0.f, 0.f);

        RCache.set_c(C, result);
    }
} binder_detector;

static class cl_hud_params final : public R_constant_setup //--#SM+#--
{
    void setup(R_constant* C) override { RCache.set_c(C, g_pGamePersistent->m_pGShaderConstants.hud_params); }
} binder_hud_params;

static class cl_script_params final : public R_constant_setup //--#SM+#--
{
    void setup(R_constant* C) override { RCache.set_c(C, g_pGamePersistent->m_pGShaderConstants.m_script_params); }
} binder_script_params;

static class cl_blend_mode final : public R_constant_setup //--#SM+#--
{
    void setup(R_constant* C) override { RCache.set_c(C, g_pGamePersistent->m_pGShaderConstants.m_blender_mode); }
} binder_blend_mode;

static class cl_ogsr_game_time final : public R_constant_setup
{
    void setup(R_constant* C) override
    {
        u32 hours{0}, mins{0}, secs{0}, milisecs{0};
        if (g_pGameLevel)
            g_pGameLevel->GetGameTimeForShaders(hours, mins, secs, milisecs);
        RCache.set_c(C, float(hours), float(mins), float(secs), float(milisecs));
    }
} binder_ogsr_game_time;

static class cl_addon_VControl final : public R_constant_setup
{
    void setup(R_constant* C) override
    {
        if (ps_r2_ls_flags_ext.test(R2FLAG_VISOR_REFL) && ps_r2_ls_flags_ext.test(R2FLAG_VISOR_REFL_CONTROL))
            RCache.set_c(C, ps_r2_visor_refl_intensity, ps_r2_visor_refl_radius, 0.f, 1.f);
        else
            RCache.set_c(C, 0.f, 0.f, 0.f, 0.f);
    }
} binder_addon_VControl;

static class cl_pda_params final : public R_constant_setup
{
    void setup(R_constant* C) override
    {
        const auto& P = shader_exports.get_pda_params();
        RCache.set_c(C, P.x, P.y, 0.f, P.z);
    }
} binder_pda_params;

static class cl_actor_params final : public R_constant_setup
{
    void setup(R_constant* C) override
    {
        const auto& P = shader_exports.get_actor_params();
        RCache.set_c(C, P.x, P.y, P.z, g_pGamePersistent->Environment().USED_COP_WEATHER ? 1.0f : 0.0f);
    }
} binder_actor_params;



static class cl_inv_v : public R_constant_setup
{
    u32 marker;
    Fmatrix result;

    virtual void setup(R_constant* C)
    {
        result.invert(Device.mView);

        RCache.set_c(C, result);
    }
} binder_inv_v;



// Sneaky debug stuff
Fvector4 ps_dev_param_1 = {0.f, 0.f, 0.f, 0.f};
Fvector4 ps_dev_param_2 = {0.f, 0.f, 0.f, 0.f};
Fvector4 ps_dev_param_3 = {0.f, 0.f, 0.f, 0.f};
Fvector4 ps_dev_param_4 = {0.f, 0.f, 0.f, 0.f};
Fvector4 ps_dev_param_5 = {0.f, 0.f, 0.f, 0.f};
Fvector4 ps_dev_param_6 = {0.f, 0.f, 0.f, 0.f};
Fvector4 ps_dev_param_7 = {0.f, 0.f, 0.f, 0.f};
Fvector4 ps_dev_param_8 = {0.f, 0.f, 0.f, 0.f};

static class dev_param_1 : public R_constant_setup
{
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_dev_param_1.x, ps_dev_param_1.y, ps_dev_param_1.z, ps_dev_param_1.w); }
} dev_param_1;

static class dev_param_2 : public R_constant_setup
{
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_dev_param_2.x, ps_dev_param_2.y, ps_dev_param_2.z, ps_dev_param_2.w); }
} dev_param_2;

static class dev_param_3 : public R_constant_setup
{
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_dev_param_3.x, ps_dev_param_3.y, ps_dev_param_3.z, ps_dev_param_3.w); }
} dev_param_3;

static class dev_param_4 : public R_constant_setup
{
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_dev_param_4.x, ps_dev_param_4.y, ps_dev_param_4.z, ps_dev_param_4.w); }
} dev_param_4;

static class dev_param_5 : public R_constant_setup
{
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_dev_param_5.x, ps_dev_param_5.y, ps_dev_param_5.z, ps_dev_param_5.w); }
} dev_param_5;

static class dev_param_6 : public R_constant_setup
{
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_dev_param_6.x, ps_dev_param_6.y, ps_dev_param_6.z, ps_dev_param_6.w); }
} dev_param_6;

static class dev_param_7 : public R_constant_setup
{
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_dev_param_7.x, ps_dev_param_7.y, ps_dev_param_7.z, ps_dev_param_7.w); }
} dev_param_7;

static class dev_param_8 : public R_constant_setup
{
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_dev_param_8.x, ps_dev_param_8.y, ps_dev_param_8.z, ps_dev_param_8.w); }
} dev_param_8;


static class ssfx_wpn_dof_1 : public R_constant_setup 
{
    virtual void setup(R_constant * C) {
        RCache.set_c(C, ps_ssfx_wpn_dof_1.x, ps_ssfx_wpn_dof_1.y, ps_ssfx_wpn_dof_1.z, ps_ssfx_wpn_dof_1.w);
    }
}
ssfx_wpn_dof_1;
 static class ssfx_wpn_dof_2:public R_constant_setup  {
     virtual void setup(R_constant * C) {
         RCache.set_c(C, ps_ssfx_wpn_dof_2, 0, 0, 0);
     }
}
ssfx_wpn_dof_2;



// Standart constant-binding
void CBlender_Compile::SetMapping()
{
    // matrices
    r_Constant("m_W", &binder_w);
    r_Constant("m_invW", &binder_invw);
    r_Constant("m_V", &binder_v);
    r_Constant("m_P", &binder_p);
    r_Constant("m_WV", &binder_wv);
    r_Constant("m_VP", &binder_vp);
    r_Constant("m_WVP", &binder_wvp);
    r_Constant("m_inv_V", &binder_inv_v);

    r_Constant("m_xform_v", &tree_binder_m_xform_v);
    r_Constant("m_xform", &tree_binder_m_xform);
    r_Constant("consts", &tree_binder_consts);
    r_Constant("wave", &tree_binder_wave);
    r_Constant("wind", &tree_binder_wind);
    r_Constant("c_scale", &tree_binder_c_scale);
    r_Constant("c_bias", &tree_binder_c_bias);
    r_Constant("c_sun", &tree_binder_c_sun);

    r_Constant("ssfx_wpn_dof_1", &ssfx_wpn_dof_1);
    r_Constant("ssfx_wpn_dof_2", &ssfx_wpn_dof_2);

    // hemi cube
    r_Constant("L_material", &binder_material);
    r_Constant("hemi_cube_pos_faces", &binder_hemi_cube_pos_faces);
    r_Constant("hemi_cube_neg_faces", &binder_hemi_cube_neg_faces);

    //	Igor	temp solution for the texgen functionality in the shader
    r_Constant("m_texgen", &binder_texgen);
    r_Constant("mVPTexgen", &binder_VPtexgen);

    // fog-params
    r_Constant("fog_plane", &binder_fog_plane);
    r_Constant("fog_params", &binder_fog_params);
    r_Constant("fog_color", &binder_fog_color);

    // Rain
    r_Constant("rain_params", &binder_rain_params);

    // time
    r_Constant("timers", &binder_times);

    // eye-params
    r_Constant("eye_position", &binder_eye_P);
    r_Constant("eye_position_lerp", &binder_eye_PL);
    r_Constant("eye_direction", &binder_eye_D);
    r_Constant("eye_direction_lerp", &binder_eye_DL);
    r_Constant("eye_normal", &binder_eye_N);

    // crookr
    r_Constant("fakescope_params1", &binder_fakescope_params);
    r_Constant("fakescope_params2", &binder_fakescope_ca);
    r_Constant("fakescope_params3", &binder_fakescope_params3);

    // global-lighting (env params)
    r_Constant("L_sun_color", &binder_sun0_color);
    r_Constant("L_sun_dir_w", &binder_sun0_dir_w);
    r_Constant("L_sun_dir_e", &binder_sun0_dir_e);
    //	r_Constant				("L_lmap_color",	&binder_lm_color);
    r_Constant("L_hemi_color", &binder_hemi_color);
    r_Constant("L_ambient", &binder_amb_color);

    r_Constant("screen_res", &binder_screen_res);
    r_Constant("ogse_c_screen", &binder_screen_params);

    r_Constant("ogse_c_artefacts", &binder_artifacts);
    r_Constant("ogse_c_artefacts2", &binder_artifacts2);
    r_Constant("ogse_c_artefacts3", &binder_artifacts3);
    r_Constant("ogse_c_anomalys", &binder_anomalys);
    r_Constant("ogse_c_anomalys2", &binder_anomalys2);
    r_Constant("ogse_c_anomalys3", &binder_anomalys3);
    r_Constant("ogse_c_detector", &binder_detector);

    // detail
    // if (bDetail	&& detail_scaler)
    //	Igor: bDetail can be overridden by no_detail_texture option.
    //	But shader can be deatiled implicitly, so try to set this parameter
    //	anyway.
    if (detail_scaler)
        r_Constant("dt_params", detail_scaler);

    // misc
    r_Constant("m_hud_params", &binder_hud_params); //--#SM+#--
    r_Constant("m_script_params", &binder_script_params); //--#SM+#--
    r_Constant("m_blender_mode", &binder_blend_mode); //--#SM+#--

    r_Constant("ogsr_game_time", &binder_ogsr_game_time);

    r_Constant("addon_VControl", &binder_addon_VControl);

    r_Constant("m_affects", &binder_pda_params);

    r_Constant("m_actor_params", &binder_actor_params);



    	// Shader stuff
    r_Constant("shader_param_1", &dev_param_1);
    r_Constant("shader_param_2", &dev_param_2);
    r_Constant("shader_param_3", &dev_param_3);
    r_Constant("shader_param_4", &dev_param_4);
    r_Constant("shader_param_5", &dev_param_5);
    r_Constant("shader_param_6", &dev_param_6);
    r_Constant("shader_param_7", &dev_param_7);
    r_Constant("shader_param_8", &dev_param_8);



    // other common
    for (const auto& [name, s] : DEV->v_constant_setup)
        r_Constant(name.c_str(), s);
}
