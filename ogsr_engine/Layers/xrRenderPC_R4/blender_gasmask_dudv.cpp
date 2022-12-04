#include "stdafx.h"

#include "blender_gasmask_dudv.h"
#include "r2_types.h"
CBlender_gasmask_dudv::CBlender_gasmask_dudv() { description.CLS = 0; }

CBlender_gasmask_dudv::~CBlender_gasmask_dudv()
{
}

void CBlender_gasmask_dudv::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	C.r_Pass("stub_screen_space", "gasmask_dudv", FALSE, FALSE, FALSE);
	C.r_dx10Texture("s_image", r2_RT_generic0);
	C.r_dx10Texture("s_mask_droplets", "shaders\\gasmasks\\mask_droplets");

	C.r_dx10Texture("s_mask_nm_1", "shaders\\gasmasks\\mask_nm_1");
	C.r_dx10Texture("s_mask_nm_2", "shaders\\gasmasks\\mask_nm_2");
	C.r_dx10Texture("s_mask_nm_3", "shaders\\gasmasks\\mask_nm_3");
	C.r_dx10Texture("s_mask_nm_4", "shaders\\gasmasks\\mask_nm_4");
	C.r_dx10Texture("s_mask_nm_5", "shaders\\gasmasks\\mask_nm_5");
	C.r_dx10Texture("s_mask_nm_6", "shaders\\gasmasks\\mask_nm_6");
	C.r_dx10Texture("s_mask_nm_7", "shaders\\gasmasks\\mask_nm_7");
	C.r_dx10Texture("s_mask_nm_8", "shaders\\gasmasks\\mask_nm_8");
	C.r_dx10Texture("s_mask_nm_9", "shaders\\gasmasks\\mask_nm_9");
	C.r_dx10Texture("s_mask_nm_10", "shaders\\gasmasks\\mask_nm_10");
	
	C.r_dx10Sampler("smp_base");
	C.r_dx10Sampler("smp_nofilter");
	C.r_dx10Sampler("smp_rtlinear");
	C.r_End();
}

CBlender_fakescope::CBlender_fakescope() { description.CLS = 0; } // crookr
CBlender_fakescope::~CBlender_fakescope() // crookr
{}
void CBlender_fakescope::Compile(CBlender_Compile& C) // crookr
{
    IBlender::Compile(C);
    C.r_Pass("stub_screen_space", "fakescope", FALSE, FALSE, FALSE);
    C.r_dx10Texture("s_position", r2_RT_P);
    C.r_dx10Texture("s_image", r2_RT_generic0);
    C.r_dx10Texture("s_bloom_new", r2_RT_pp_bloom);
    C.r_dx10Texture("s_blur_2", r2_RT_blur_2);
    C.r_dx10Texture("s_blur_4", r2_RT_blur_4);
    C.r_dx10Texture("s_blur_8", r2_RT_blur_8);
    // C.r_dx10Texture("s_scope", "wpn\\wpn_crosshair_pso1");
    // C.r_dx10Texture("s_scope", scope_fake_texture);
    C.r_dx10Texture("s_scope", r2_RT_scopert);
    C.r_dx10Sampler("smp_base");
    C.r_dx10Sampler("smp_nofilter");
    C.r_dx10Sampler("smp_rtlinear");
    C.r_End();
}