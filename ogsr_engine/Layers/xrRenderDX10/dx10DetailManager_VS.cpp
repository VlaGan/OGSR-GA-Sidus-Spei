#include "stdafx.h"
#include "../xrRender/DetailManager.h"

#include "../../xr_3da/igame_persistent.h"
#include "../../xr_3da/environment.h"

#include "../xrRenderDX10/dx10BufferUtils.h"

const int quant = 16384;
const int c_hdr = 10;
const int c_size = 4;

//#define SIMPLE_DETAIL_COLLISION //-- упрощённая коллизия травы

static D3DVERTEXELEMENT9 dwDecl[] = {{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0}, // pos
                                     {0, 12, D3DDECLTYPE_SHORT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0}, // uv
                                     D3DDECL_END()};

#pragma pack(push, 1)
struct vertHW
{
    float x, y, z;
    short u, v, t, mid;
};
#pragma pack(pop)

short QC(float v);
//{
//	int t=iFloor(v*float(quant)); clamp(t,-32768,32767);
//	return short(t&0xffff);
//}

void CDetailManager::hw_Load_Shaders()
{
    // Create shader to access constant storage
    ref_shader S;
    S.create("details\\set");
    R_constant_table& T0 = *(S->E[0]->passes[0]->constants);
    R_constant_table& T1 = *(S->E[1]->passes[0]->constants);
    hwc_consts = T0.get("consts");
    hwc_wave = T0.get("wave");
    hwc_wind = T0.get("dir2D");
    hwc_array = T0.get("array");
    hwc_s_consts = T1.get("consts");
    hwc_s_xform = T1.get("xform");
    hwc_s_array = T1.get("array");
}

void CDetailManager::hw_Render()
{
    // Render-prepare
    //	Update timer
    //	Can't use Device.fTimeDelta since it is smoothed! Don't know why, but smoothed value looks more choppy!
    float fDelta = Device.fTimeGlobal - m_global_time_old;
    if ((fDelta < 0) || (fDelta > 1))
        fDelta = 0.03;
    m_global_time_old = Device.fTimeGlobal;

    m_time_rot_1 += (PI_MUL_2 * fDelta / swing_current.rot1);
    m_time_rot_2 += (PI_MUL_2 * fDelta / swing_current.rot2);
    m_time_pos += fDelta * swing_current.speed;

    // float		tm_rot1		= (PI_MUL_2*Device.fTimeGlobal/swing_current.rot1);
    // float		tm_rot2		= (PI_MUL_2*Device.fTimeGlobal/swing_current.rot2);
    float tm_rot1 = m_time_rot_1;
    float tm_rot2 = m_time_rot_2;

    Fvector4 dir1, dir2;
    dir1.set(_sin(tm_rot1), 0, _cos(tm_rot1), 0).normalize().mul(swing_current.amp1);
    dir2.set(_sin(tm_rot2), 0, _cos(tm_rot2), 0).normalize().mul(swing_current.amp2);

    // Setup geometry and DMA
    RCache.set_Geometry(hw_Geom);

    // Wave0
    float scale = 1.f / float(quant);
    Fvector4 wave;
    Fvector4 consts;
    consts.set(scale, scale, ps_r__Detail_l_aniso, ps_r__Detail_l_ambient);
    // wave.set				(1.f/5.f,		1.f/7.f,	1.f/3.f,	Device.fTimeGlobal*swing_current.speed);
    wave.set(1.f / 5.f, 1.f / 7.f, 1.f / 3.f, m_time_pos);
    // RCache.set_c			(&*hwc_consts,	scale,		scale,		ps_r__Detail_l_aniso,	ps_r__Detail_l_ambient);				// consts
    // RCache.set_c			(&*hwc_wave,	wave.div(PI_MUL_2));	// wave
    // RCache.set_c			(&*hwc_wind,	dir1);																					// wind-dir
    // hw_Render_dump			(&*hwc_array,	1, 0, c_hdr );
    hw_Render_dump(consts, wave.div(PI_MUL_2), dir1, 1, 0);

    // Wave1
    // wave.set				(1.f/3.f,		1.f/7.f,	1.f/5.f,	Device.fTimeGlobal*swing_current.speed);
    wave.set(1.f / 3.f, 1.f / 7.f, 1.f / 5.f, m_time_pos);
    // RCache.set_c			(&*hwc_wave,	wave.div(PI_MUL_2));	// wave
    // RCache.set_c			(&*hwc_wind,	dir2);																					// wind-dir
    // hw_Render_dump			(&*hwc_array,	2, 0, c_hdr );
    hw_Render_dump(consts, wave.div(PI_MUL_2), dir2, 2, 0);

    // Still
    consts.set(scale, scale, scale, 1.f);
    // RCache.set_c			(&*hwc_s_consts,scale,		scale,		scale,				1.f);
    // RCache.set_c			(&*hwc_s_xform,	Device.mFullTransform);
    // hw_Render_dump			(&*hwc_s_array,	0, 1, c_hdr );
    hw_Render_dump(consts, wave.div(PI_MUL_2), dir2, 0, 1);
}


static constexpr auto deg2radfactor = PI / 180.f; // = 0.017453292f;

void CDetailManager::hw_Render_dump(const Fvector4& consts, const Fvector4& wave, const Fvector4& wind, u32 var_id, u32 lod_id)
{
    static shared_str strConsts("consts");
    static shared_str strWave("wave");
    static shared_str strDir2D("dir2D");
    static shared_str strArray("array");
    static shared_str strXForm("xform");

    Device.Statistic->RenderDUMP_DT_Count = 0;

    // Matrices and offsets
    u32 vOffset = 0;
    u32 iOffset = 0;

    vis_list& list = m_visibles[var_id];

    CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
    Fvector c_sun, c_ambient, c_hemi;
    c_sun.set(desc.sun_color.x, desc.sun_color.y, desc.sun_color.z);
    c_sun.mul(.5f);
    c_ambient.set(desc.ambient.x, desc.ambient.y, desc.ambient.z);
    c_hemi.set(desc.hemi_color.x, desc.hemi_color.y, desc.hemi_color.z);

    float lost_parent_timeout = (ps_detail_collision_time * 1.5f);

    //-- лучше вынести это сюда
    Fvector ymp = ps_detail_collision_angle;
    Fmatrix mGrassCollusionRot, M;
    DetailCollusionPoint* pPointParent = nullptr;

    // Iterate
    for (u32 O = 0; O < objects.size(); O++)
    {
        CDetail& Object = *objects[O];
        xr_vector<SlotItemVec*>& vis = list[O];
        if (!vis.empty())
        {
            for (u32 iPass = 0; iPass < Object.shader->E[lod_id]->passes.size(); ++iPass)
            {
                // Setup matrices + colors (and flush it as necessary)
                // RCache.set_Element				(Object.shader->E[lod_id]);
                RCache.set_Element(Object.shader->E[lod_id], iPass);
                RImplementation.apply_lmaterial();

                //	This could be cached in the corresponding consatant buffer
                //	as it is done for DX9
                RCache.set_c(strConsts, consts);
                RCache.set_c(strWave, wave);
                RCache.set_c(strDir2D, wind);
                RCache.set_c(strXForm, Device.mFullTransform);

                // ref_constant constArray = RCache.get_c(strArray);
                // VERIFY(constArray);

                // u32			c_base				= x_array->vs.index;
                // Fvector4*	c_storage			= RCache.get_ConstantCache_Vertex().get_array_f().access(c_base);
                Fvector4* c_storage = 0;
                //	Map constants to memory directly
                {
                    void* pVData;
                    RCache.get_ConstantDirect(strArray, hw_BatchSize * sizeof(Fvector4) * 4, &pVData, 0, 0);
                    c_storage = (Fvector4*)pVData;
                }
                VERIFY(c_storage);

                u32 dwBatch = 0;

                xr_vector<SlotItemVec*>::iterator _vI = vis.begin();
                xr_vector<SlotItemVec*>::iterator _vE = vis.end();
                for (; _vI != _vE; _vI++)
                {
                    SlotItemVec* items = *_vI;
                    SlotItemVecIt _iI = items->begin();
                    SlotItemVecIt _iE = items->end();
                    for (; _iI != _iE; _iI++)
                    {
                        if (*_iI == nullptr)
                            continue;

                        SlotItem& Instance = **_iI;
                        u32 base = dwBatch * 4;

                        // Build matrix ( 3x4 matrix, last row - color )
                        float scale = Instance.scale_calculated;
                        M = Instance.mRotY;

                        //-----------------------------------------------------------------------
                        //-- VlaGan: псевдоколизия травы ----------------------------------------
                        //-----------------------------------------------------------------------
                        if (ps_detail_enable_collision && M.c.distance_to(actor_position) < ps_detail_collision_radius)
                        {

//-- единственное отличие упрощённой колизии в том, что
//-- что трава не будет коллизировать с другими точками, пока не
//-- вернётся в исходное положение, это меньше цпу лоад
#ifndef SIMPLE_DETAIL_COLLISION
                            //-- Если id точки колизии не существует, но есть трава с такой - сбрасываем ид
                            if (Instance.IsCollisionParent())
                            {
                                pPointParent = GetDetailCollusionPointById(Instance.collusion_parent);
                                if (!pPointParent)
                                    Instance.collusion_parent = (u16)-1;
                            }

                            for (auto& point : level_detailcoll_points)
                            {
                                //-- Если у нас уже есть ид того, кто коллизирует
                                //-- с травой и он не равен текущему point.id
                                //-- + дальше чем его радиус коллизии - пропускаем
                                if (pPointParent)
                                {
                                    if ((pPointParent->pos.distance_to(M.c) <= (pPointParent->radius + (pPointParent->radius * 0.3f))) && (point.id != Instance.collusion_parent))
                                        continue;

                                    if (Instance.collusion_parent != point.id && point.pos.distance_to(M.c) > point.radius)
                                        continue;
                                }

                                if (point.pos.distance_to(M.c) <= point.radius && point.id == Instance.collusion_parent)
                                {
                                    Instance.m_fTimeCollusion += Device.fTimeDelta / point.rot_time_in;
                                }
                                else
                                {
                                    if (Instance.IsCollisionParent())
                                    {
                                        //-- Переопределяем владельца, если трава начала возвращаться в исходное
                                        //-- положение от воздействия предыдущего, иначе, она просто бы не реагировала
                                        //-- на другие точки колизии, что выглядит так себе

                                        if (Instance.collusion_parent != point.id && point.pos.distance_to(M.c) <= point.radius)
                                        {
                                            Instance.collusion_parent = point.id;
                                            break;
                                        }

                                        if (Instance.collusion_parent == point.id)
                                            Instance.m_fTimeCollusion -= Device.fTimeDelta / point.rot_time_out;
                                    }
                                    else
                                    {
                                        //-- присваиванием id обьекта, что коллизирует с травой
                                        //-- вообще, вся эта мутка с ид нужна для того, чтобы на данную траву не
                                        //-- влияли все точки колизии
                                        if (point.pos.distance_to(M.c) <= point.radius)
                                        {
                                            Instance.collusion_parent = point.id;
                                            Instance.m_fTimeCollusion += Device.fTimeDelta / point.rot_time_in;
                                        }

                                        //-- имеем поворот, нокаким-то хером потеряли парента и не нашли нового - закостылим,
                                        //-- чтобы все поинты моментально не опустили эту траву
                                        if (Instance.m_fTimeCollusion && !Instance.IsCollisionParent())
                                            Instance.m_fTimeCollusion -= Device.fTimeDelta / (lost_parent_timeout / level_detailcoll_points.size());
                                    }
                                }
                                //-- Удаляем парента
                                if (!Instance.m_fTimeCollusion)
                                    Instance.collusion_parent = (u16)-1;
                                //-- это для коллизии от разных воздействий, чтобы проверить
                                //-- что эта точка коллизирует хотя бы с одной травинкой
                                if (point.is_explosion)
                                    if (Instance.collusion_parent == point.id && !point.bNoExplCollision)
                                        point.bNoExplCollision = true;
                            }
#else
                            for (auto& point : level_detailcoll_points)
                            {
                                //-- Удаляем парента
                                //-- если удалять снизу, как сверху, то почему-то 
                                //-- трава не проминаеться от других коллизионеров, кроме того
                                //-- кто первый её примнул
                                if (!Instance.m_fTimeCollusion)
                                    Instance.collusion_parent = (u16)-1;

                                //-- упрощённая колизия
                                if (!Instance.IsCollisionParent())
                                {
                                    if (point.pos.distance_to(M.c) <= point.radius)
                                        Instance.collusion_parent = point.id;
                                    else
                                    {
                                        if (Instance.m_fTimeCollusion)
                                            Instance.m_fTimeCollusion -= Device.fTimeDelta / (lost_parent_timeout / level_detailcoll_points.size());
                                        continue;
                                    }
                                }
                                else
                                {
                                    if (point.id != Instance.collusion_parent)
                                        continue;
                                }


                                //-- по идее, сюда попадает только точка в которой point.id == Instance.collusion_parent
                                if (point.pos.distance_to(M.c) <= point.radius)
                                    Instance.m_fTimeCollusion += Device.fTimeDelta / point.rot_time_in;
                                else
                                    Instance.m_fTimeCollusion -= Device.fTimeDelta / point.rot_time_out;

                                //-- это для коллизии от разных воздействий, чтобы проверить
                                //-- что эта точка коллизирует хотя бы с одной травинкой
                                if (point.is_explosion)
                                    if (Instance.collusion_parent == point.id && !point.bNoExplCollision)
                                        point.bNoExplCollision = true;
                            }
#endif

#pragma todo("VlaGan: попробовать научить траву крутиться от точки коллизии в разные стороны по её радису.")
                            //CALC_OFFSETS : {
                            //-- Нема смысла считать поворот для одной травы от каждой точки (- жпу лоад)
                            //-- упростил сам вид вычислений поворота травы (ну и по памяти приятней будет)
                            clamp(Instance.m_fTimeCollusion, 0.f, 1.f);
                            mGrassCollusionRot.identity();
                            ymp.mul(deg2radfactor * Instance.m_fTimeCollusion);
                            mGrassCollusionRot.setHPB(ymp.x, ymp.y, ymp.z);
                            ymp = ps_detail_collision_angle;
                            M.mulB_43(mGrassCollusionRot);
                            //}

                            pPointParent = nullptr;
                        }
                        else
                        {
                            Instance.m_fTimeCollusion = 0.f;
                            Instance.collusion_parent = (u16)-1;
                        }

                        c_storage[base + 0].set(M._11 * scale, M._21 * scale, M._31 * scale, M._41);
                        c_storage[base + 1].set(M._12 * scale, M._22 * scale, M._32 * scale, M._42);
                        c_storage[base + 2].set(M._13 * scale, M._23 * scale, M._33 * scale, M._43);
                        // RCache.set_ca(&*constArray, base+0, M._11*scale,	M._21*scale,	M._31*scale,	M._41	);
                        // RCache.set_ca(&*constArray, base+1, M._12*scale,	M._22*scale,	M._32*scale,	M._42	);
                        // RCache.set_ca(&*constArray, base+2, M._13*scale,	M._23*scale,	M._33*scale,	M._43	);

                        // Build color
                        // R2 only needs hemisphere
                        float h = Instance.c_hemi;
                        float s = Instance.c_sun;
                        c_storage[base + 3].set(s, s, s, h);
                        // RCache.set_ca(&*constArray, base+3, s,				s,				s,				h		);
                        dwBatch++;
                        if (dwBatch == hw_BatchSize)
                        {
                            // flush
                            Device.Statistic->RenderDUMP_DT_Count += dwBatch;
                            u32 dwCNT_verts = dwBatch * Object.number_vertices;
                            u32 dwCNT_prims = (dwBatch * Object.number_indices) / 3;
                            // RCache.get_ConstantCache_Vertex().b_dirty				=	TRUE;
                            // RCache.get_ConstantCache_Vertex().get_array_f().dirty	(c_base,c_base+dwBatch*4);
                            RCache.Render(D3DPT_TRIANGLELIST, vOffset, 0, dwCNT_verts, iOffset, dwCNT_prims);
                            RCache.stat.r.s_details.add(dwCNT_verts);

                            // restart
                            dwBatch = 0;

                            //	Remap constants to memory directly (just in case anything goes wrong)
                            {
                                void* pVData;
                                RCache.get_ConstantDirect(strArray, hw_BatchSize * sizeof(Fvector4) * 4, &pVData, 0, 0);
                                c_storage = (Fvector4*)pVData;
                            }
                            VERIFY(c_storage);
                        }
                    }
                }
                // flush if nessecary
                if (dwBatch)
                {
                    Device.Statistic->RenderDUMP_DT_Count += dwBatch;
                    u32 dwCNT_verts = dwBatch * Object.number_vertices;
                    u32 dwCNT_prims = (dwBatch * Object.number_indices) / 3;
                    // RCache.get_ConstantCache_Vertex().b_dirty				=	TRUE;
                    // RCache.get_ConstantCache_Vertex().get_array_f().dirty	(c_base,c_base+dwBatch*4);
                    RCache.Render(D3DPT_TRIANGLELIST, vOffset, 0, dwCNT_verts, iOffset, dwCNT_prims);
                    RCache.stat.r.s_details.add(dwCNT_verts);
                }
            }
        }
        vOffset += hw_BatchSize * Object.number_vertices;
        iOffset += hw_BatchSize * Object.number_indices;
    }

    //-- VlaGan: тут фокус в том, что при каждом апдейте перемещаем
    //-- колизии от взрыва и тп немного вниз, а когда видим, что с ней ничего
    //-- не коллизирует - удаляем, но можно и сразу в ебеня, но тогда
    //-- не будет плавного перехода
    //-- перебрал дохера способов, но этот вроде +- оптимальный
    if (ps_detail_enable_collision)
    for (auto& point : level_detailcoll_points)
        if (point.is_explosion)
            if (!point.bNoExplCollision)
                point.is_explosion = false;
            else
            {
                if (point.fExplCollisionTimeIn >= point.rot_time_in)
                { 
                    //-- Плавный переход
                    //-- убираем предыдущее смещение
                    point.pos.y += point.radius * point.fExplCollisionTimeOut;

                    //-- считаем и добавляем новое
                    point.fExplCollisionTimeOut += Device.fTimeDelta / point.fExpPointLoweringTime;
                    point.pos.y -= point.radius * point.fExplCollisionTimeOut;
                    
                    
                    //-- Просто и понятно, но без плавного перехода до центра
                    //point.pos.y = -250.f;
                }
                else
                    point.fExplCollisionTimeIn += Device.fTimeDelta / point.rot_time_in;

                point.bNoExplCollision = false;
            }
}
