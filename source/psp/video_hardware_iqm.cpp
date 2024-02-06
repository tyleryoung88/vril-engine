extern"C" {
    #include "../quakedef.h"
}

#include <pspgu.h>
#include <pspgum.h>

#include "video_hardware_iqm.h"


#define VERT_BONES 4
#define TRI_VERTS 3
#define SUBMESH_BONES 8



// 
// Returns true if `bone_idx` is in list `bone_list`
// 
bool bone_in_list(uint8_t bone_idx, const uint8_t *bone_list, const int n_bones) {
    for(int i = 0; i < n_bones; i++) {
        if(bone_idx == bone_list[i]) {
            return true;
        }
    }
    return false;
}

//
// Returns true if list `bone_list_a` is a subset of `bone_list_b`
//
bool bone_list_is_subset(const uint8_t *bone_list_a, const int bone_list_a_n_bones, const uint8_t *bone_list_b, const int bone_list_b_n_bones) {
    for(int i = 0; i < bone_list_a_n_bones; i++) {
        bool bone_in_list = false;
        for(int j = 0; j < bone_list_b_n_bones; j++) {
            if(bone_list_a[i] == bone_list_b[j]) {
                bone_in_list = true;
                break;
            }
        }
        // If any bone was missing from list b, not a subset
        if(!bone_in_list) {
            return false;
        }
    }
    // All `a` bones were found in `b`, `a` is a subset of `b`.
    return true;
}

//
// Returns the number of bones that are in `bone_list_a` but not in `bone_list_b`
//  i.e. len(set(bone_list_a) - set(bone_list_b))
//
int bone_list_set_difference(const uint8_t *bone_list_a, const int bone_list_a_n_bones, const uint8_t *bone_list_b, const int bone_list_b_n_bones) {
    int n_missing_bones = 0;

    for(int i = 0; i < bone_list_a_n_bones; i++) {
        bool bone_in_list = false;
        for(int j = 0; j < bone_list_b_n_bones; j++) {
            if(bone_list_a[i] == bone_list_b[j]) {
                bone_in_list = true;
                break;
            }
        }
        // If bone was missing from list b, count it
        if(!bone_in_list) {
            n_missing_bones += 1;
        }
    }
    return n_missing_bones;
}

//
// Performs the set union of bones in both `bone_list_a` and `bone_list_b`
// Writes the union into `bone_list_a`
//  i.e. len(set(bone_list_a) + set(bone_list_b))
//
// WARNING: Assumes `bone_list_a` has the capacity for the union of both lists.
//
int bone_list_set_union(uint8_t *bone_list_a, int bone_list_a_n_bones, const uint8_t *bone_list_b, const int bone_list_b_n_bones) {
    for(int i = 0; i < bone_list_b_n_bones; i++) {
        bool bone_already_in_a = false;

        for(int j = 0; j < bone_list_a_n_bones; j++) {
            if(bone_list_a[j] == bone_list_b[i]) {
                bone_already_in_a = true;
                break;
            }
        }
        if(bone_already_in_a) {
            continue;
        }

        bone_list_a[bone_list_a_n_bones] = bone_list_b[i];
        bone_list_a_n_bones += 1;
    }
    return bone_list_a_n_bones;
}



#define UINT16_T_MAX_VAL     65535
#define UINT16_T_MIN_VAL     0
#define INT16_T_MAX_VAL      32767
#define INT16_T_MIN_VAL     -32768
#define UINT8_T_MAX_VAL      255
#define UINT8_T_MIN_VAL      0
#define INT8_T_MAX_VAL       127
#define INT8_T_MIN_VAL      -128

// 
// Maps a float in [-1,1] to a int16_t in [-32768, 32767]
//
int16_t float_to_int16(float x) {
    // Rescale to [0,1]
    x = ((x+1) * 0.5);
    // Interpolate between min and max value
    int16_t val = (int16_t) (INT16_T_MIN_VAL + x * (INT16_T_MAX_VAL - INT16_T_MIN_VAL));
    // Clamp to bounds
    return std::min( (int16_t) INT16_T_MAX_VAL, std::max( (int16_t) INT16_T_MIN_VAL, val ));
}

// 
// Maps a float in [0,1] to a uint16_t in [0, 65535]
//
uint16_t float_to_uint16(float x) {
    // Interpolate between min and max value
    uint16_t val = (int16_t) (UINT16_T_MIN_VAL + x * (UINT16_T_MAX_VAL - UINT16_T_MIN_VAL));
    // Clamp to bounds
    return std::min( (uint16_t) UINT16_T_MAX_VAL, std::max( (uint16_t) UINT16_T_MIN_VAL, val ));
}

// 
// Maps a float in [-1,1] to a int8_t in [-128, 127]
//
int8_t float_to_int8(float x) {
    // Rescale to [0,1]
    x = ((x+1) * 0.5);
    // Interpolate between min and max value
    int8_t val = (int8_t) (INT8_T_MIN_VAL + x * (INT8_T_MAX_VAL - INT8_T_MIN_VAL));
    // Clamp to bounds
    return std::min( (int8_t) INT8_T_MAX_VAL, std::max( (int8_t) INT8_T_MIN_VAL, val ));
}

//
// Applies the inverse of an ofs and a scale
//
float apply_inv_ofs_scale(float x, float ofs, float scale) {
    return (x - ofs) / scale;
}

// 
// Maps a float in [0,1] to a uint8_t in [0, 255]
//
uint8_t float_to_uint8(float x) {
    // Interpolate between min and max value
    uint8_t val = (uint8_t) (UINT8_T_MIN_VAL + x * (UINT8_T_MAX_VAL - UINT8_T_MIN_VAL));
    // Clamp to bounds
    return std::min( (uint8_t) UINT8_T_MAX_VAL, std::max( (uint8_t) UINT8_T_MIN_VAL, val ));
}




// 
// Splits a `skeletal_mesh_t` mesh into one or more `skeletal_mesh_t` submeshes that reference no more than 8 bones
//
void submesh_skeletal_mesh(skeletal_mesh_t *mesh) {
    Con_Printf("=========== Submesh started =============\n");

    // --------------------------------------------------------------------
    // Build the set of bones referenced by each triangle
    // --------------------------------------------------------------------
    uint8_t *tri_n_bones = (uint8_t*) malloc(sizeof(uint8_t) * mesh->n_tris); // Contains the number of bones that the i-th triangle references
    uint8_t *tri_bones = (uint8_t*) malloc(sizeof(uint8_t) * TRI_VERTS * VERT_BONES * mesh->n_tris); // Contains the list of bones referenced by the i-th triangle


    for(uint32_t tri_idx = 0; tri_idx < mesh->n_tris; tri_idx++) {
        // Initialize this triangle's bone list to 0s
        for(int tri_bone_idx = 0; tri_bone_idx < TRI_VERTS * VERT_BONES; tri_bone_idx++) {
            tri_bones[(tri_idx * TRI_VERTS * VERT_BONES) + tri_bone_idx] = 0;
        }
        tri_n_bones[tri_idx] = 0;

        for(uint32_t tri_vert_idx = 0; tri_vert_idx < TRI_VERTS; tri_vert_idx++ ) {
            uint32_t vert_idx = mesh->tri_verts[(tri_idx * TRI_VERTS) + tri_vert_idx];
            // Loop through the vertex's referenced bones
            for(int vert_bone_idx = 0; vert_bone_idx < VERT_BONES; vert_bone_idx++) {
                uint8_t bone_idx = mesh->vert_bone_idxs[vert_idx * VERT_BONES + vert_bone_idx];
                float bone_weight = mesh->vert_bone_weights[vert_idx * VERT_BONES + vert_bone_idx];

                if(bone_weight > 0) {
                    // Verify the bone is not already in this triangle's bone list
                    if(!bone_in_list(bone_idx, &tri_bones[tri_idx * TRI_VERTS * VERT_BONES], tri_n_bones[tri_idx])) {
                        tri_bones[(tri_idx * TRI_VERTS * VERT_BONES) + tri_n_bones[tri_idx]] = bone_idx;
                        tri_n_bones[tri_idx] += 1;
                    }
                }
            }
        }
    }
    // --------------------------------------------------------------------


    // // Debug print a few triangle bone lists:
    // for(int j = 0; j < 10; j++) {
    //     Con_Printf("Mesh: %d tri: %d bones (%d): ", i, j, tri_n_bones[j]);
    //     for(int k = 0; k < tri_n_bones[j]; k++) {
    //         Con_Printf("%d, ", tri_bones[j * TRI_VERTS * VERT_BONES + k]);
    //     }
    //     Con_Printf("\n");
    // }
    // break;

    // --------------------------------------------------------------------
    // Assign each triangle in the mesh to a submesh idx
    // --------------------------------------------------------------------
    int8_t *tri_submesh_idx = (int8_t*) malloc(sizeof(int8_t) * mesh->n_tris); // Contains the set the i-th triangle belongs to
    const int SET_DISCARD = -2; // Discarded triangles
    const int SET_UNASSIGNED = -1; // Denotes unassigned triangles
    for(uint32_t j = 0; j < mesh->n_tris; j++) {
        tri_submesh_idx[j] = SET_UNASSIGNED;
    }

    int cur_submesh_idx = -1;

    while(true) {
        // Find the unassigned triangle that uses the most bones:
        int cur_tri = -1;
        for(uint32_t tri_idx = 0; tri_idx < mesh->n_tris; tri_idx++) {
            // If this triangle isn't `UNASSIGNED`, skip it
            if(tri_submesh_idx[tri_idx] != SET_UNASSIGNED) {
                continue;
            }
            // If we haven't found one yet, set it
            if(cur_tri == -1) {
                cur_tri = tri_idx;
                continue;
            }
            // If this triangle references more bones, update cur_tri
            if(tri_n_bones[tri_idx] > tri_n_bones[cur_tri]) {
                cur_tri = tri_idx;
            }
        }

        // If we didn't find any triangles, stop submesh algorithm. We're done.
        if(cur_tri == -1) {
            break;
        }

        cur_submesh_idx += 1;
        int cur_submesh_n_bones = 0;
        uint8_t *cur_submesh_bones = (uint8_t*) malloc(sizeof(uint8_t) * SUBMESH_BONES);
        Con_Printf("Creating submesh %d for mesh\n", cur_submesh_idx);

        // Verify the triangle doesn't have more than the max bones allowed:
        if(tri_n_bones[cur_tri] > SUBMESH_BONES) {
            Con_Printf("Warning: Mesh Triangle %d references %d bones, which is more than the maximum allowed for any mesh (%d). Skipping triangle...\n", cur_tri, tri_n_bones[cur_tri], SUBMESH_BONES);
            // Discard it
            tri_submesh_idx[cur_tri] = SET_DISCARD;
            continue;
        }

        // Add the triangle to the current submesh:
        tri_submesh_idx[cur_tri] = cur_submesh_idx;

        // Add the triangle's bones to the current submesh:
        for(int submesh_bone_idx = 0; submesh_bone_idx < tri_n_bones[cur_tri]; submesh_bone_idx++) {
            cur_submesh_bones[submesh_bone_idx] = tri_bones[(cur_tri * TRI_VERTS * VERT_BONES) + submesh_bone_idx];
            cur_submesh_n_bones += 1;
        }

        Con_Printf("\tstart submesh bones (%d): [", cur_submesh_n_bones);
        for(int submesh_bone_idx = 0; submesh_bone_idx < cur_submesh_n_bones; submesh_bone_idx++) {
            Con_Printf("%d, ", cur_submesh_bones[submesh_bone_idx]);
        }
        Con_Printf("]\n");

        // Add all unassigned triangles from the main mesh that references bones in this submesh's bone list
        for(uint32_t tri_idx = 0; tri_idx < mesh->n_tris; tri_idx++) {
            // If this triangle isn't `UNASSIGNED`, skip it
            if(tri_submesh_idx[tri_idx] != SET_UNASSIGNED) {
                continue;
            }

            // if(i == 0) {
            //     Con_Printf("Mesh %d submesh %d checking tri %d\n",i,cur_submesh_idx,tri_idx);
            //     Con_Printf("\tTri bones: (%d), ", tri_n_bones[tri_idx]);
            //     for(int tri_bone_idx = 0; tri_bone_idx < tri_n_bones[tri_idx]; tri_bone_idx++) {
            //         Con_Printf("%d,", tri_bones[(tri_idx * TRI_VERTS * VERT_BONES) + tri_bone_idx]);
            //     }
            //     Con_Printf("\n");
            // }

            // If this triangle's bones is not a subset of the current submesh bone list, skip it
            if(!bone_list_is_subset(&tri_bones[tri_idx * TRI_VERTS * VERT_BONES], tri_n_bones[tri_idx], cur_submesh_bones, cur_submesh_n_bones)) {
                continue;
            }

            // Otherwise, it is a subset, add it to the current submesh
            tri_submesh_idx[tri_idx] = cur_submesh_idx;
        }


        // Print how many triangles belong to the current submesh
        int cur_submesh_n_tris = 0;
        int n_assigned_tris = 0;
        for(uint32_t tri_idx = 0; tri_idx < mesh->n_tris; tri_idx++) {
            if(tri_submesh_idx[tri_idx] != SET_UNASSIGNED) {
                n_assigned_tris++;
            }
            if(tri_submesh_idx[tri_idx] == cur_submesh_idx) {
                cur_submesh_n_tris++;
            }
        }
        Con_Printf("\tcur submesh (%d) n_tris: %d/%d, remaining unassigned: %d/%d\n", cur_submesh_idx, cur_submesh_n_tris, mesh->n_tris, n_assigned_tris, mesh->n_tris);


        // Repeat until there are no unassigned triangles remaining:
        while(true) {
            // Get triangle with the minimum number of bones not in the current submesh bone list
            cur_tri = -1;
            int cur_tri_n_missing_bones = 0;
            for(uint32_t tri_idx = 0; tri_idx < mesh->n_tris; tri_idx++) {
                // If this triangle isn't `UNASSIGNED`, skip it
                if(tri_submesh_idx[tri_idx] != SET_UNASSIGNED) {
                    continue;
                }
                // Count the number of bones referenced by this triangle that are not in the current submesh bone list
                int n_missing_bones = bone_list_set_difference(&tri_bones[tri_idx * TRI_VERTS * VERT_BONES], tri_n_bones[tri_idx], cur_submesh_bones, cur_submesh_n_bones);
                if(cur_tri == -1 || n_missing_bones < cur_tri_n_missing_bones) {
                    cur_tri = tri_idx;
                    cur_tri_n_missing_bones = n_missing_bones;
                }
            }

            // If no triangle found, stop. We're done.
            if(cur_tri == -1) {
                Con_Printf("\tNo more unassigned triangles. Done with mesh.\n");
                break;
            }

            // If this triangle pushes us past the submesh-bone limit, we are done with this submesh. Move onto the next one.
            if(cur_submesh_n_bones + cur_tri_n_missing_bones > SUBMESH_BONES) {
                Con_Printf("\tReached max number of bones allowed. Done with submesh.\n");
                break;
            }

            Con_Printf("\tNext loop using triangle: %d, missing bones: %d\n", cur_tri, cur_tri_n_missing_bones);


            // Assign the triangle to the current submesh
            tri_submesh_idx[cur_tri] = cur_submesh_idx;

            // Add this triangle's bones to the current submesh list of bones
            cur_submesh_n_bones = bone_list_set_union( cur_submesh_bones, cur_submesh_n_bones, &tri_bones[cur_tri * TRI_VERTS * VERT_BONES], tri_n_bones[cur_tri]);


            Con_Printf("\tcur submesh bones (%d): [", cur_submesh_n_bones);
            for(int submesh_bone_idx = 0; submesh_bone_idx < cur_submesh_n_bones; submesh_bone_idx++) {
                Con_Printf("%d, ", cur_submesh_bones[submesh_bone_idx]);
            }
            Con_Printf("]\n");


            // Add all unassigned triangles from the main mesh that reference bones in this submesh's bone list
            for(uint32_t tri_idx = 0; tri_idx < mesh->n_tris; tri_idx++) {
                // If this triangle isn't `UNASSIGNED`, skip it
                if(tri_submesh_idx[tri_idx] != SET_UNASSIGNED) {
                    continue;
                }

                // if(i == 0) {
                //     Con_Printf("Mesh %d submesh %d checking tri %d\n",i,cur_submesh_idx,tri_idx);
                //     Con_Printf("\tTri bones: (%d), ", tri_n_bones[tri_idx]);
                //     for(int tri_bone_idx = 0; tri_bone_idx < tri_n_bones[tri_idx]; tri_bone_idx++) {
                //         Con_Printf("%d,", tri_bones[(tri_idx * TRI_VERTS * VERT_BONES) + tri_bone_idx]);
                //     }
                //     Con_Printf("\n");
                // }

                // If this triangle's bones is not a subset of the current submesh bone list, skip it
                if(!bone_list_is_subset(&tri_bones[tri_idx * TRI_VERTS * VERT_BONES], tri_n_bones[tri_idx], cur_submesh_bones, cur_submesh_n_bones)) {
                    continue;
                }

                // Otherwise, it is a subset, add it to the current submesh
                tri_submesh_idx[tri_idx] = cur_submesh_idx;
            }

            // Print how many triangles belong to the current submesh
            cur_submesh_n_tris = 0;
            n_assigned_tris = 0;
            for(uint32_t tri_idx = 0; tri_idx < mesh->n_tris; tri_idx++) {
                if(tri_submesh_idx[tri_idx] != SET_UNASSIGNED) {
                    n_assigned_tris++;
                }
                if(tri_submesh_idx[tri_idx] == cur_submesh_idx) {
                    cur_submesh_n_tris++;
                }
            }
            Con_Printf("\tDone adding new tris for cur triangle");
            Con_Printf("\tcur submesh (%d) n_tris: %d/%d, total assigned: %d/%d\n", cur_submesh_idx, cur_submesh_n_tris, mesh->n_tris, n_assigned_tris, mesh->n_tris);
        }

        free(cur_submesh_bones);
    }

    int n_submeshes = cur_submesh_idx + 1;
    mesh->n_submeshes = n_submeshes;
    mesh->submeshes = (skeletal_mesh_t *) malloc(sizeof(skeletal_mesh_t) * n_submeshes);


    for(int submesh_idx = 0; submesh_idx < n_submeshes; submesh_idx++) {
        Con_Printf("Reconstructing submesh %d/%d for mesh\n", submesh_idx+1, n_submeshes);
        // Count the number of triangles that have been assigned to this submesh
        int submesh_tri_count = 0;
        for(uint32_t tri_idx = 0; tri_idx < mesh->n_tris; tri_idx++) {
            if(tri_submesh_idx[tri_idx] == submesh_idx) {
                submesh_tri_count++;
            }
        }

        // Allocate enough memory to fit theoretical max amount of unique vertes this model can reference (given we know its triangle count)
        uint16_t *submesh_mesh_vert_idxs = (uint16_t*) malloc(sizeof(uint16_t) * TRI_VERTS * submesh_tri_count); // Indices into mesh list of vertices
        // Allocate enough memoery to fit 3 vertex indices per triangle
        uint16_t *submesh_tri_verts = (uint16_t*) malloc(sizeof(uint16_t) * TRI_VERTS * submesh_tri_count);
        uint32_t submesh_n_tris = 0;
        uint32_t submesh_n_verts = 0;

        // ----------------------------------------------------------------
        // Build this submesh's list of triangle indices, vertex list, and
        // triangle vertex indices
        // ----------------------------------------------------------------
        for(uint32_t mesh_tri_idx = 0; mesh_tri_idx < mesh->n_tris; mesh_tri_idx++) {
            // Skip triangles that don't belong to this submesh
            if(tri_submesh_idx[mesh_tri_idx] != submesh_idx) {
                continue;
            }

            // Add the triangle to our submesh's list of triangles
            int submesh_tri_idx = submesh_n_tris;
            submesh_n_tris += 1;

            // Add each of the triangle's verts to the submesh list of verts
            // If that vertex is already in the submesh, use that index instead
            for(int tri_vert_idx = 0; tri_vert_idx < TRI_VERTS; tri_vert_idx++) {
                int mesh_vert_idx = mesh->tri_verts[(mesh_tri_idx * TRI_VERTS) + tri_vert_idx];
                // FIXME - This is a pointer into the full model vert indices list... Do we instead want the index into the mesh's vertex list?

                // Check if this vertex is already in the submesh
                int submesh_vert_idx = -1;
                for(uint32_t j = 0; j < submesh_n_verts; j++) {
                    if(submesh_mesh_vert_idxs[j] == mesh_vert_idx) {
                        submesh_vert_idx = j;
                        break;
                    }
                }
                // If we didn't find the vertex in the submesh vertex list, add it
                if(submesh_vert_idx == -1) {
                    submesh_vert_idx = submesh_n_verts;
                    submesh_mesh_vert_idxs[submesh_n_verts] = mesh_vert_idx;
                    submesh_n_verts += 1;
                }

                // Store the submesh vert idx for this triangle
                submesh_tri_verts[(submesh_tri_idx * TRI_VERTS) + tri_vert_idx] = submesh_vert_idx;
            }
        }
        // ----------------------------------------------------------------

        mesh->submeshes[submesh_idx].n_verts = submesh_n_verts;
        mesh->submeshes[submesh_idx].n_tris = submesh_n_tris;
        mesh->submeshes[submesh_idx].vert_rest_positions = (vec3_t*) malloc(sizeof(vec3_t) * submesh_n_verts);
        mesh->submeshes[submesh_idx].vert_uvs = (vec2_t*) malloc(sizeof(vec2_t) * submesh_n_verts);
        mesh->submeshes[submesh_idx].vert_rest_normals = (vec3_t*) malloc(sizeof(vec3_t) * submesh_n_verts);
        // mesh->submeshes[submesh_idx].vert_bone_weights = (float*) malloc(sizeof(float) * VERT_BONES * submesh_n_verts);
        // mesh->submeshes[submesh_idx].vert_bone_idxs = (uint8_t*) malloc(sizeof(uint8_t) * VERT_BONES * submesh_n_verts);
        mesh->submeshes[submesh_idx].vert_bone_weights = nullptr;
        mesh->submeshes[submesh_idx].vert_bone_idxs = nullptr;
        mesh->submeshes[submesh_idx].vert16s = nullptr;
        mesh->submeshes[submesh_idx].vert8s = nullptr;
        mesh->submeshes[submesh_idx].vert_skinning_weights = (float*) malloc(sizeof(float) * SUBMESH_BONES * submesh_n_verts);
        mesh->submeshes[submesh_idx].verts_ofs[0] = 0.0f;
        mesh->submeshes[submesh_idx].verts_ofs[1] = 0.0f;
        mesh->submeshes[submesh_idx].verts_ofs[2] = 0.0f;
        mesh->submeshes[submesh_idx].verts_scale[0] = 1.0f;
        mesh->submeshes[submesh_idx].verts_scale[1] = 1.0f;
        mesh->submeshes[submesh_idx].verts_scale[2] = 1.0f;
        mesh->submeshes[submesh_idx].n_submeshes = 0;
        mesh->submeshes[submesh_idx].submeshes = nullptr;



        for(uint32_t vert_idx = 0; vert_idx < submesh_n_verts; vert_idx++) {
            uint16_t mesh_vert_idx = submesh_mesh_vert_idxs[vert_idx]; 
            mesh->submeshes[submesh_idx].vert_rest_positions[vert_idx][0] = mesh->vert_rest_positions[mesh_vert_idx][0];
            mesh->submeshes[submesh_idx].vert_rest_positions[vert_idx][1] = mesh->vert_rest_positions[mesh_vert_idx][1];
            mesh->submeshes[submesh_idx].vert_rest_positions[vert_idx][2] = mesh->vert_rest_positions[mesh_vert_idx][2];
            mesh->submeshes[submesh_idx].vert_uvs[vert_idx][0] = mesh->vert_uvs[mesh_vert_idx][0];
            mesh->submeshes[submesh_idx].vert_uvs[vert_idx][1] = mesh->vert_uvs[mesh_vert_idx][1];
            mesh->submeshes[submesh_idx].vert_rest_normals[vert_idx][0] = mesh->vert_rest_normals[mesh_vert_idx][0];
            mesh->submeshes[submesh_idx].vert_rest_normals[vert_idx][1] = mesh->vert_rest_normals[mesh_vert_idx][1];
            mesh->submeshes[submesh_idx].vert_rest_normals[vert_idx][2] = mesh->vert_rest_normals[mesh_vert_idx][2];

            // for(int k = 0; k < VERT_BONES; k++) {
            //     mesh->submeshes[submesh_idx].vert_bone_weights[(vert_idx * VERT_BONES) + k] = mesh->vert_bone_weights[(mesh_vert_idx * VERT_BONES) + k];
            //     mesh->submeshes[submesh_idx].vert_bone_idxs[(vert_idx * VERT_BONES) + k] = mesh->vert_bone_idxs[(mesh_vert_idx * VERT_BONES) + k];
            // }

            // Initialize all bone skinning weights to 0.0f
            for(int bone_idx = 0; bone_idx < SUBMESH_BONES; bone_idx++) {
                mesh->submeshes[submesh_idx].vert_skinning_weights[vert_idx * SUBMESH_BONES + bone_idx] = 0.0f;
            }
        }


        // FIXME - Why write them if we immediately free them? Are these used?
        free(mesh->submeshes[submesh_idx].vert_bone_idxs);
        free(mesh->submeshes[submesh_idx].vert_bone_weights);
        mesh->submeshes[submesh_idx].vert_bone_idxs = nullptr;
        mesh->submeshes[submesh_idx].vert_bone_weights = nullptr;

        // ----------------------------------------------------------------
        // Build the submesh's list of bones indices, and the vertex skinning weights
        // ----------------------------------------------------------------
        int n_submesh_bones = 0;
        uint8_t *submesh_bones = (uint8_t*) malloc(sizeof(uint8_t) * SUBMESH_BONES);
        // Initialize to all 0s
        for(int bone_idx = 0; bone_idx < SUBMESH_BONES; bone_idx++) {
            submesh_bones[bone_idx] = 0;
        }

        // For every vertex
        for(uint32_t vert_idx = 0; vert_idx < submesh_n_verts; vert_idx++) {
            int mesh_vert_idx = submesh_mesh_vert_idxs[vert_idx];

            // For every bone that belongs to that vertex
            for(int vert_bone_idx = 0; vert_bone_idx < VERT_BONES; vert_bone_idx++) {
                uint8_t bone_idx = mesh->vert_bone_idxs[mesh_vert_idx * VERT_BONES + vert_bone_idx];
                float bone_weight = mesh->vert_bone_weights[mesh_vert_idx * VERT_BONES + vert_bone_idx];
                if(bone_weight > 0.0f) {
                    int vert_bone_submesh_idx = -1;
                    // Search the submesh's list of bones for this bone
                    for(int submesh_bone_idx = 0; submesh_bone_idx < n_submesh_bones; submesh_bone_idx++) {
                        if(bone_idx == submesh_bones[submesh_bone_idx]) {
                            vert_bone_submesh_idx = submesh_bone_idx;
                            break;
                        }
                    }

                    // If we didn't find the bone, add it to the submesh's list of bones
                    if(vert_bone_submesh_idx == -1) {
                        submesh_bones[n_submesh_bones] = bone_idx;
                        vert_bone_submesh_idx = n_submesh_bones;
                        n_submesh_bones += 1;
                    }

                    // Set the vertex's corresponding weight entry for that bone
                    mesh->submeshes[submesh_idx].vert_skinning_weights[vert_idx * SUBMESH_BONES + vert_bone_submesh_idx] = bone_weight;
                }
            }
        }


        // TEMP HACK DEBUG
        // Set each vertex bone skinning weights to [1.0, 0.0, 0.0, ...]
        // for(uint32_t vert_idx = 0; vert_idx < submesh_n_verts; vert_idx++) {
        //     mesh->submeshes[submesh_idx].vert_skinning_weights[vert_idx * SUBMESH_BONES + 0] = 1.0f;
        //     for(int submesh_bone_idx = 1; submesh_bone_idx < SUBMESH_BONES; submesh_bone_idx++) {
        //         mesh->submeshes[submesh_idx].vert_skinning_weights[vert_idx * SUBMESH_BONES + submesh_bone_idx] = 0.0f;
        //     }
        // }

        // Save the submesh's list of bone indices
        mesh->submeshes[submesh_idx].n_skinning_bones = n_submesh_bones;
        for(int bone_idx = 0; bone_idx < SUBMESH_BONES; bone_idx++) {
            mesh->submeshes[submesh_idx].skinning_bone_idxs[bone_idx] = submesh_bones[bone_idx];
        }
        // ----------------------------------------------------------------


        // ----------------------------------------------------------------
        // Set the triangle vertex indices
        // ----------------------------------------------------------------
        mesh->submeshes[submesh_idx].tri_verts = (uint16_t*) malloc(sizeof(uint16_t) * TRI_VERTS * submesh_n_tris);
        for(uint32_t tri_idx = 0; tri_idx < submesh_n_tris; tri_idx++) {
            mesh->submeshes[submesh_idx].tri_verts[tri_idx * TRI_VERTS + 0] = submesh_tri_verts[tri_idx * TRI_VERTS + 0];
            mesh->submeshes[submesh_idx].tri_verts[tri_idx * TRI_VERTS + 1] = submesh_tri_verts[tri_idx * TRI_VERTS + 1];
            mesh->submeshes[submesh_idx].tri_verts[tri_idx * TRI_VERTS + 2] = submesh_tri_verts[tri_idx * TRI_VERTS + 2];
        }
        // ----------------------------------------------------------------


        // -––-----------––-----------––-----------––-----------––---------
        Con_Printf("Mesh submesh %d bones (%d): [", submesh_idx, n_submesh_bones);
        for(int j = 0; j < SUBMESH_BONES; j++) {
            Con_Printf("%d, ", submesh_bones[j]);
        }
        Con_Printf("]\n");
        // // TODO - Print all verts for this submesh
        // for(uint32_t vert_idx = 0; vert_idx < submesh_n_verts; vert_idx++) {
        //     int mesh_vert_idx = submesh_mesh_vert_idxs[vert_idx];
        //     Con_Printf("\tvert %d, bones {%d:%.2f, %d:%.2f, %d:%.2f, %d:%.2f} --> [%d:%.2f, %d:%.2f, %d:%.2f, %d:%.2f, %d:%.2f, %d:%.2f, %d:%.2f, %d:%.2f]\n", 
        //         vert_idx, 
        //         skel_model->meshes[i].vert_bone_idxs[mesh_vert_idx * VERT_BONES + 0], skel_model->meshes[i].vert_bone_weights[mesh_vert_idx * VERT_BONES + 0],
        //         skel_model->meshes[i].vert_bone_idxs[mesh_vert_idx * VERT_BONES + 1], skel_model->meshes[i].vert_bone_weights[mesh_vert_idx * VERT_BONES + 1],
        //         skel_model->meshes[i].vert_bone_idxs[mesh_vert_idx * VERT_BONES + 2], skel_model->meshes[i].vert_bone_weights[mesh_vert_idx * VERT_BONES + 2],
        //         skel_model->meshes[i].vert_bone_idxs[mesh_vert_idx * VERT_BONES + 3], skel_model->meshes[i].vert_bone_weights[mesh_vert_idx * VERT_BONES + 3],
        //         submesh_bones[0], skel_model->meshes[i].submeshes[submesh_idx].skinning_verts[vert_idx].bone_weights[0],
        //         submesh_bones[1], skel_model->meshes[i].submeshes[submesh_idx].skinning_verts[vert_idx].bone_weights[1],
        //         submesh_bones[2], skel_model->meshes[i].submeshes[submesh_idx].skinning_verts[vert_idx].bone_weights[2],
        //         submesh_bones[3], skel_model->meshes[i].submeshes[submesh_idx].skinning_verts[vert_idx].bone_weights[3],
        //         submesh_bones[4], skel_model->meshes[i].submeshes[submesh_idx].skinning_verts[vert_idx].bone_weights[4],
        //         submesh_bones[5], skel_model->meshes[i].submeshes[submesh_idx].skinning_verts[vert_idx].bone_weights[5],
        //         submesh_bones[6], skel_model->meshes[i].submeshes[submesh_idx].skinning_verts[vert_idx].bone_weights[6],
        //         submesh_bones[7], skel_model->meshes[i].submeshes[submesh_idx].skinning_verts[vert_idx].bone_weights[7]
        //     );
        // }
        // -––-----------––-----------––-----------––-----------––---------

        Con_Printf("About to free submesh data structures...\n");
        free(submesh_mesh_vert_idxs);
        free(submesh_tri_verts);
    }

    free(tri_n_bones);
    free(tri_bones);
    free(tri_submesh_idx);
    // --------------------------------------------------------------------
}




//
// Parses an IQM Vertex array and converts all values to floats
//
void iqm_parse_float_array(const void *iqm_data, const iqm_vert_array_t *vert_array, float *out, size_t n_elements, size_t element_len, float *default_value) {
    if(vert_array == nullptr) {
        return;
    }
    iqm_dtype dtype = (iqm_dtype) vert_array->format;
    size_t iqm_values_to_read = (size_t) vert_array->size;
    if(vert_array->ofs == 0) {
        iqm_values_to_read = 0;
        dtype = iqm_dtype::IQM_DTYPE_FLOAT;
    }
    const uint8_t *iqm_array_data = (const uint8_t*) iqm_data + vert_array->ofs;


    // Special cases:
    if(dtype == iqm_dtype::IQM_DTYPE_FLOAT && element_len == iqm_values_to_read) {
        memcpy(out, (const float*) iqm_array_data, sizeof(float) * element_len * n_elements);
        return;
    }
    if(dtype == iqm_dtype::IQM_DTYPE_HALF) {
        iqm_values_to_read = 0;
    }

    // For all other dtype cases, parse each value from IQM:
    for(size_t i = 0; i < n_elements; i++) {
        // Read the first `iqm_values_to_read` values for vector `i`
        for(size_t j = 0; j < element_len && j < iqm_values_to_read; j++) {
            switch(dtype) {
                default:
                    iqm_values_to_read = 0;
                    break;
                case iqm_dtype::IQM_DTYPE_BYTE:
                    out[i * element_len + j] = ((const int8_t*)iqm_array_data)[i * iqm_values_to_read + j] * (1.0f/127);
                    break;
                case iqm_dtype::IQM_DTYPE_UBYTE:
                    out[i * element_len + j] = ((const uint8_t*)iqm_array_data)[i * iqm_values_to_read + j] * (1.0f/255);
                    break;
                case iqm_dtype::IQM_DTYPE_SHORT:
                    out[i * element_len + j] = ((const int16_t*)iqm_array_data)[i * iqm_values_to_read + j] * (1.0f/32767);
                    break;
                case iqm_dtype::IQM_DTYPE_USHORT:
                    out[i * element_len + j] = ((const uint16_t*)iqm_array_data)[i * iqm_values_to_read + j] * (1.0f/65535);
                    break;
                case iqm_dtype::IQM_DTYPE_INT:
                    out[i * element_len + j] = ((const int32_t*)iqm_array_data)[i * iqm_values_to_read + j]  / ((float)0x7fffffff);
                    break;
                case iqm_dtype::IQM_DTYPE_UINT:
                    out[i * element_len + j] = ((const uint32_t*)iqm_array_data)[i * iqm_values_to_read + j] / ((float)0xffffffffu);
                    break;
                case iqm_dtype::IQM_DTYPE_FLOAT:
                    out[i * element_len + j] = ((const float*)iqm_array_data)[i * iqm_values_to_read + j];
                    break;
                case iqm_dtype::IQM_DTYPE_DOUBLE:
                    out[i * element_len + j] = ((const double*)iqm_array_data)[i * iqm_values_to_read + j];
                    break;
            }
        }
        // Pad the remaining `element_len - iqm_values_to_read` values for vector `i`
        for(size_t j = iqm_values_to_read; j < element_len; j++) {
            out[i * element_len + j] = default_value[j];
        }
    }
}


//
// Parses an IQM Vertex array and converts all values to uint8_t
//
void iqm_parse_uint8_array(const void *iqm_data, const iqm_vert_array_t *vert_array, uint8_t *out, size_t n_elements, size_t element_len, uint8_t max_value) {
    if(vert_array == nullptr) {
        return;
    }
    iqm_dtype dtype = (iqm_dtype) vert_array->format;
    size_t iqm_values_to_read = (size_t) vert_array->size;
    if(vert_array->ofs == 0) {
        iqm_values_to_read = 0;
        dtype = iqm_dtype::IQM_DTYPE_UBYTE;
    }
    const uint8_t *iqm_array_data = (const uint8_t*) iqm_data + vert_array->ofs;



    // Special cases:
    if(dtype == iqm_dtype::IQM_DTYPE_FLOAT && element_len == iqm_values_to_read) {
        memcpy(out, (const float*) iqm_array_data, sizeof(float) * element_len * n_elements);
        return;
    }
    if(dtype == iqm_dtype::IQM_DTYPE_HALF) {
        iqm_values_to_read = 0;
    }

    // For all other dtype cases, parse each value from IQM:
    for(size_t i = 0; i < n_elements; i++) {
        // Read the first `iqm_values_to_read` values for vector `i`
        for(size_t j = 0; j < element_len && j < iqm_values_to_read; j++) {

            uint8_t in_val;
            switch(dtype) {
                case iqm_dtype::IQM_DTYPE_FLOAT:    // Skip, these values don't make sense
                case iqm_dtype::IQM_DTYPE_DOUBLE:   // Skip, these values don't make sense
                default:
                    in_val = 0;
                    iqm_values_to_read = 0;
                    break;
                case iqm_dtype::IQM_DTYPE_BYTE:     // Interpret as signed
                case iqm_dtype::IQM_DTYPE_UBYTE:
                    in_val = ((const uint8_t*)iqm_array_data)[i * iqm_values_to_read + j];
                    break;
                case iqm_dtype::IQM_DTYPE_SHORT:    // Interpret as signed
                case iqm_dtype::IQM_DTYPE_USHORT:
                    in_val = (uint8_t) ((const uint16_t*)iqm_array_data)[i * iqm_values_to_read + j];
                    break;
                case iqm_dtype::IQM_DTYPE_INT:      // Interpret as signed
                case iqm_dtype::IQM_DTYPE_UINT:
                    in_val = (uint8_t) ((const uint32_t*)iqm_array_data)[i * iqm_values_to_read + j];
                    break;
            }

            if(in_val >= max_value) {
                // TODO - Mark invalid, return that array had invalid values
                in_val = 0;
            }
            out[i * element_len + j] = in_val;
        }
        // Pad the remaining `element_len - iqm_values_to_read` values for vector `i`
        for(size_t j = iqm_values_to_read; j < element_len; j++) {
            out[i * element_len + j] = 0;
        }
    }
}

//
// Builds and returns a 3x4 transform matrix from the corresponding translation vector, rotation quaternion, and scale vector
//
void Matrix3x4_scale_rotate_translate(mat3x4_t out, const vec3_t scale, const quat_t rotation, const vec3_t translation) {
    // First scale, then rotate, then translate:
    mat3x4_t rotate_translate_mat;
    Matrix3x4_FromOriginQuat(rotate_translate_mat, rotation, translation);
    mat3x4_t scale_mat;
    Matrix3x4_LoadIdentity(scale_mat);
    scale_mat[0][0] = scale[0];
    scale_mat[1][1] = scale[1];
    scale_mat[2][2] = scale[2];
    Matrix3x4_ConcatTransforms(out, rotate_translate_mat, scale_mat);
}


// 
// Using temporary data loaded by `load_iqm_file`, writes the vertex structs that:
//      - Use 8-bit, 16-bit, or both vertex sizes
//      - Remove triangle indices to have a raw list of unindexed vertices
// 
void write_skinning_vert_structs(skeletal_mesh_t *mesh, bool write_vert8s, bool write_vert16s) {
    // ----------------------------------------------------------------------
    // 8-bit and 16-bit vertices are quantized onto a grid
    // Find the most compact grid to quantize the vertices onto
    // ----------------------------------------------------------------------
    // NOTE - This creates a quantization grid per-mesh that wraps the mesh 
    // NOTE   in the rest position. For software skinning, the mesh moves 
    // NOTE   beyond this quantization grid, causing problems.
    // NOTE - Though this could work for hardware skinning setups.
    Con_Printf("Calculating quantization ofs / scale for mesh...\n");

    // Compute mins / maxs along each axis (x,y,z)
    // Then compute ofs and scale to have all verts on that axis be centered on 
    // the origin and fit within [-1, 1]
    for(int axis=0; axis < 3; axis++) {
        float model_min = mesh->vert_rest_positions[0][axis];
        float model_max = mesh->vert_rest_positions[0][axis];
        for(uint32_t vert_idx = 0; vert_idx < mesh->n_verts; vert_idx++) {
            model_min = std::min(mesh->vert_rest_positions[vert_idx][axis], model_min);
            model_max = std::max(mesh->vert_rest_positions[vert_idx][axis], model_max);
        }
        mesh->verts_ofs[axis] = ((model_max - model_min) / 2.0f) + model_min;
        mesh->verts_scale[axis] = (model_max - model_min) / 2.0f;
    }
    Con_Printf("Done calculating quantization ofs / scale for mesh\n");
    // ----------------------------------------------------------------------

    Con_Printf("------------------------------------\n");
    Con_Printf("Quick debug before writing vert structs\n");
    Con_Printf("------------------------------------\n");
    Con_Printf("mesh->n_verts = %d\n", mesh->n_verts);
    Con_Printf("mesh->n_tris = %d\n", mesh->n_tris);
    Con_Printf("mesh->vert_rest_positions = %d\n", mesh->vert_rest_positions);
    Con_Printf("mesh->vert_uvs = %d\n", mesh->vert_uvs);
    Con_Printf("mesh->vert_rest_normals = %d\n", mesh->vert_rest_normals);
    Con_Printf("mesh->vert_bone_weights = %d\n", mesh->vert_bone_weights);
    Con_Printf("mesh->vert_bone_idxs = %d\n", mesh->vert_bone_idxs);
    Con_Printf("mesh->vert_skinning_weights = %d\n", mesh->vert_skinning_weights);
    Con_Printf("mesh->tri_verts = %d\n", mesh->tri_verts);
    Con_Printf("mesh->vert16s = %d\n", mesh->vert16s);
    Con_Printf("mesh->vert8s = %d\n", mesh->vert8s);
    Con_Printf("mesh->n_skinning_bones = %d\n", mesh->n_skinning_bones);
    Con_Printf("mesh->skinning_bone_idxs = [");
    for(int k = 0; k < 8; k++) {
        Con_Printf("%d, ", mesh->skinning_bone_idxs[k]);
    }
    Con_Printf("]\n");
    Con_Printf("mesh->verts_ofs = [");
    for(int k = 0; k < 3; k++) {
        Con_Printf("%f, ", mesh->verts_ofs[k]);
    }
    Con_Printf("]\n");
    Con_Printf("mesh->verts_scale = [");
    for(int k = 0; k < 3; k++) {
        Con_Printf("%f, ", mesh->verts_scale[k]);
    }
    Con_Printf("]\n");
    Con_Printf("mesh->n_submeshes = %d\n", mesh->n_submeshes);
    Con_Printf("mesh->submeshes = %d\n", mesh->submeshes);
    Con_Printf("------------------------------------\n");



    int n_unindexed_verts = mesh->n_tris * TRI_VERTS;
    mesh->n_verts = n_unindexed_verts;
    if(write_vert8s) {
        mesh->vert8s = (skel_vertex_i8_t*) malloc(sizeof(skel_vertex_i8_t) * n_unindexed_verts);
    }
    if(write_vert16s) {
        mesh->vert16s = (skel_vertex_i16_t*) malloc(sizeof(skel_vertex_i16_t) * n_unindexed_verts);
    }

    Con_Printf("------------------------------------\n");
    Con_Printf("Update after malloc:\n");
    Con_Printf("mesh->n_verts = %d\n", mesh->n_verts);
    Con_Printf("mesh->vert16s = %d\n", mesh->vert16s);
    Con_Printf("mesh->vert8s = %d\n", mesh->vert8s);
    Con_Printf("------------------------------------\n");

    for(uint32_t tri_idx = 0; tri_idx < mesh->n_tris; tri_idx++) {
        for(int tri_vert_idx = 0; tri_vert_idx < TRI_VERTS; tri_vert_idx++) {
            int unindexed_vert_idx = tri_idx * 3 + tri_vert_idx;
            uint16_t indexed_vert_idx = mesh->tri_verts[tri_idx * 3 + tri_vert_idx];

            if(write_vert8s) {
                // int8 vertex with int8 weights
                // Con_Printf("Writing vert8 for vert %d/%d (for triangle: %d/%d)\n", unindexed_vert_idx+1, n_unindexed_verts, tri_idx+1,mesh->n_tris);
                // Con_Printf("\twriting position...\n");
                mesh->vert8s[unindexed_vert_idx].x = float_to_int8(apply_inv_ofs_scale(mesh->vert_rest_positions[indexed_vert_idx][0], mesh->verts_ofs[0], mesh->verts_scale[0]));
                mesh->vert8s[unindexed_vert_idx].y = float_to_int8(apply_inv_ofs_scale(mesh->vert_rest_positions[indexed_vert_idx][1], mesh->verts_ofs[1], mesh->verts_scale[1]));
                mesh->vert8s[unindexed_vert_idx].z = float_to_int8(apply_inv_ofs_scale(mesh->vert_rest_positions[indexed_vert_idx][2], mesh->verts_ofs[2], mesh->verts_scale[2]));
                // Con_Printf("\twriting uv...\n");
                mesh->vert8s[unindexed_vert_idx].u = float_to_int8(mesh->vert_uvs[indexed_vert_idx][0]);
                mesh->vert8s[unindexed_vert_idx].v = float_to_int8(mesh->vert_uvs[indexed_vert_idx][1]);
                // Con_Printf("\twriting normal...\n");
                mesh->vert8s[unindexed_vert_idx].nor_x = float_to_int8(mesh->vert_rest_normals[indexed_vert_idx][0]);
                mesh->vert8s[unindexed_vert_idx].nor_y = float_to_int8(mesh->vert_rest_normals[indexed_vert_idx][1]);
                mesh->vert8s[unindexed_vert_idx].nor_z = float_to_int8(mesh->vert_rest_normals[indexed_vert_idx][2]);
                // Con_Printf("\twriting bone weights...\n");
                for(int bone_idx = 0; bone_idx < SUBMESH_BONES; bone_idx++) {
                    mesh->vert8s[unindexed_vert_idx].bone_weights[bone_idx] = float_to_int8(mesh->vert_skinning_weights[indexed_vert_idx * SUBMESH_BONES + bone_idx]);
                }
            }
            if(write_vert16s) {
                // int16 vertex with int8 weights
                // Con_Printf("Writing vert16 for vert %d/%d (for triangle: %d/%d)\n", unindexed_vert_idx+1, n_unindexed_verts, tri_idx+1,mesh->n_tris);
                // Con_Printf("\twriting position...\n");
                mesh->vert16s[unindexed_vert_idx].x = float_to_int16(apply_inv_ofs_scale(mesh->vert_rest_positions[indexed_vert_idx][0], mesh->verts_ofs[0], mesh->verts_scale[0]));
                mesh->vert16s[unindexed_vert_idx].y = float_to_int16(apply_inv_ofs_scale(mesh->vert_rest_positions[indexed_vert_idx][1], mesh->verts_ofs[1], mesh->verts_scale[1]));
                mesh->vert16s[unindexed_vert_idx].z = float_to_int16(apply_inv_ofs_scale(mesh->vert_rest_positions[indexed_vert_idx][2], mesh->verts_ofs[2], mesh->verts_scale[2]));
                // Con_Printf("\twriting uv...\n");
                mesh->vert16s[unindexed_vert_idx].u = float_to_int16(mesh->vert_uvs[indexed_vert_idx][0]);
                mesh->vert16s[unindexed_vert_idx].v = float_to_int16(mesh->vert_uvs[indexed_vert_idx][1]);
                // Con_Printf("\twriting normal...\n");
                mesh->vert16s[unindexed_vert_idx].nor_x = float_to_int16(mesh->vert_rest_normals[indexed_vert_idx][0]);
                mesh->vert16s[unindexed_vert_idx].nor_y = float_to_int16(mesh->vert_rest_normals[indexed_vert_idx][1]);
                mesh->vert16s[unindexed_vert_idx].nor_z = float_to_int16(mesh->vert_rest_normals[indexed_vert_idx][2]);
                // Con_Printf("\twriting bone weights...\n");
                for(int bone_idx = 0; bone_idx < SUBMESH_BONES; bone_idx++) {
                    mesh->vert16s[unindexed_vert_idx].bone_weights[bone_idx] = float_to_int8(mesh->vert_skinning_weights[indexed_vert_idx * SUBMESH_BONES + bone_idx]);
                }
            }
        }
    }
}


// 
// Util function that frees a pointer and sets it to nullptr
// 
void free_pointer_and_clear(void **ptr) {
    if(*ptr != nullptr) {
        free(*ptr);
        *ptr = nullptr;
    }
}


//
// Given pointer to raw IQM model bytes buffer, loads the IQM model
// 
skeletal_model_t *load_iqm_file(void *iqm_data) {
    const iqm_header_t *iqm_header = (const iqm_header_t*) iqm_data;

    if(memcmp(iqm_header->magic, IQM_MAGIC, sizeof(iqm_header->magic))) {
        return nullptr; 
    }
    if(iqm_header->version != IQM_VERSION_2) {
        return nullptr;
    }

    // We won't know what the `iqm_data` buffer length is, but this is a useful check
    // if(iqm_header->filesize != file_len) {
    //     return nullptr;
    // }
    size_t file_len = iqm_header->filesize;

    const iqm_vert_array_t *iqm_verts_pos = nullptr;
    const iqm_vert_array_t *iqm_verts_uv = nullptr;
    const iqm_vert_array_t *iqm_verts_nor = nullptr;
    const iqm_vert_array_t *iqm_verts_tan = nullptr;
    const iqm_vert_array_t *iqm_verts_color = nullptr;
    const iqm_vert_array_t *iqm_verts_bone_idxs = nullptr;
    const iqm_vert_array_t *iqm_verts_bone_weights = nullptr;


    Con_Printf("\tParsing vert attribute arrays\n");

    const iqm_vert_array_t *vert_arrays = (const iqm_vert_array_t*)((uint8_t*) iqm_data + iqm_header->ofs_vert_arrays);
    for(unsigned int i = 0; i < iqm_header->n_vert_arrays; i++) {
        if((iqm_vert_array_type) vert_arrays[i].type == iqm_vert_array_type::IQM_VERT_POS) {
            iqm_verts_pos = &vert_arrays[i];
        }
        else if((iqm_vert_array_type) vert_arrays[i].type == iqm_vert_array_type::IQM_VERT_UV) {
            iqm_verts_uv = &vert_arrays[i];
        }
        else if((iqm_vert_array_type) vert_arrays[i].type == iqm_vert_array_type::IQM_VERT_NOR) {
            iqm_verts_nor = &vert_arrays[i];
        }
        else if((iqm_vert_array_type) vert_arrays[i].type == iqm_vert_array_type::IQM_VERT_TAN) {
            // Only use tangents if float and if each tangent is a 4D vector:
            if((iqm_dtype) vert_arrays[i].format == iqm_dtype::IQM_DTYPE_FLOAT && vert_arrays[i].size == 4) {
                iqm_verts_tan = &vert_arrays[i];
            }
            else {
                Con_Printf("Warning: IQM vertex normals array (idx: %d, type: %d, fmt: %d, size: %d) is not 4D float array.\n", i, vert_arrays[i].type, vert_arrays[i].format, vert_arrays[i].size);
            }
        }
        else if((iqm_vert_array_type) vert_arrays[i].type == iqm_vert_array_type::IQM_VERT_COLOR) {
            iqm_verts_color = &vert_arrays[i];
        }
        else if((iqm_vert_array_type) vert_arrays[i].type == iqm_vert_array_type::IQM_VERT_BONE_IDXS) {
            iqm_verts_bone_idxs = &vert_arrays[i];
        }
        else if((iqm_vert_array_type) vert_arrays[i].type == iqm_vert_array_type::IQM_VERT_BONE_WEIGHTS) {
            iqm_verts_bone_weights = &vert_arrays[i];
        }
        else {
            Con_Printf("Warning: Unrecognized IQM vertex array type (idx: %d, type: %d, fmt: %d, size: %d)\n", i, vert_arrays[i].type, vert_arrays[i].format, vert_arrays[i].size);
        }
    }

    Con_Printf("\tCreating skeletal model object\n");
    skeletal_model_t *skel_model = (skeletal_model_t*) malloc(sizeof(skeletal_model_t));
    skel_model->n_meshes = iqm_header->n_meshes;
    skel_model->meshes = (skeletal_mesh_t*) malloc(sizeof(skeletal_mesh_t) * skel_model->n_meshes);

    // FIXME - Should we even store these? Should we automatically convert them down?
    vec2_t *verts_uv = (vec2_t*) malloc(sizeof(vec2_t) * iqm_header->n_verts);
    vec3_t *verts_pos = (vec3_t*) malloc(sizeof(vec3_t) * iqm_header->n_verts);
    vec3_t *verts_nor = (vec3_t*) malloc(sizeof(vec3_t) * iqm_header->n_verts);


    // ------------------------------------------------------------------------
    // Convert verts_pos / verts_uv datatypes to floats 
    // ------------------------------------------------------------------------
    vec3_t default_vert = {0,0,0};
    vec2_t default_uv = {0,0};
    vec3_t default_nor = {0,0,1.0f};

    Con_Printf("\tParsing vertex attribute arrays\n");
    Con_Printf("Verts pos: %d\n", iqm_verts_pos);
    Con_Printf("Verts uv: %d\n", iqm_verts_uv);
    Con_Printf("Verts nor: %d\n", iqm_verts_nor);
    Con_Printf("Verts bone weights: %d\n", iqm_verts_bone_weights);
    Con_Printf("Verts bone idxs: %d\n", iqm_verts_bone_idxs);
    iqm_parse_float_array(iqm_data, iqm_verts_pos, (float*) verts_pos, iqm_header->n_verts, 3, (float*) &default_vert);
    iqm_parse_float_array(iqm_data, iqm_verts_uv,  (float*) verts_uv,  iqm_header->n_verts, 2, (float*) &default_uv);
    iqm_parse_float_array(iqm_data, iqm_verts_nor, (float*) verts_nor, iqm_header->n_verts, 3, (float*) &default_nor);

    float *verts_bone_weights = (float*)   malloc(sizeof(float) * 4 * iqm_header->n_verts);
    uint8_t *verts_bone_idxs  = (uint8_t*) malloc(sizeof(uint8_t) * 4 * iqm_header->n_verts);
    float default_bone_weights[] = {0.0f, 0.0f, 0.0f, 0.0f};
    iqm_parse_float_array(iqm_data, iqm_verts_bone_weights, verts_bone_weights,  iqm_header->n_verts, 4, (float*) &default_bone_weights);
    iqm_parse_uint8_array(iqm_data, iqm_verts_bone_idxs,    verts_bone_idxs,     iqm_header->n_verts, 4, (uint8_t) std::min( (int) iqm_header->n_joints, (int) IQM_MAX_BONES));

    // TODO - Parse / set other vertex attributes we care about:
    // - vertex normals <-  iqm_verts_nor
    // - vertex tangents <- iqm_verts_tan
    // - vertex colors <-   iqm_verts_color
    const iqm_mesh_t *iqm_meshes = (const iqm_mesh_t*)((uint8_t*) iqm_data + iqm_header->ofs_meshes);

    for(uint32_t i = 0; i < iqm_header->n_meshes; i++) {
        skeletal_mesh_t *mesh = &skel_model->meshes[i];
        const char *material_name = (const char*) (((uint8_t*) iqm_data + iqm_header->ofs_text) + iqm_meshes[i].material);
        // Con_Printf("Mesh[%d]: \"%s\"\n", i, material_name);

        uint32_t first_vert = iqm_meshes[i].first_vert_idx;
        uint32_t first_tri = iqm_meshes[i].first_tri;
        // skel_model->meshes[i].first_vert = first_vert;
        uint32_t n_verts = iqm_meshes[i].n_verts;
        mesh->n_verts = n_verts;
        mesh->vert_rest_positions = (vec3_t*) malloc(sizeof(vec3_t) * n_verts);
        mesh->vert_rest_normals = (vec3_t*) malloc(sizeof(vec3_t) * n_verts);
        mesh->vert_uvs = (vec2_t*) malloc(sizeof(vec2_t) * n_verts);
        mesh->vert_bone_weights = (float*) malloc(sizeof(float)  * VERT_BONES * n_verts);  // 4 bone weights per vertex
        mesh->vert_bone_idxs = (uint8_t*) malloc(sizeof(uint8_t) * VERT_BONES * n_verts);  // 4 bone indices per vertex
        mesh->vert_skinning_weights = nullptr;
        mesh->verts_ofs[0] = 0.0f;
        mesh->verts_ofs[1] = 0.0f;
        mesh->verts_ofs[2] = 0.0f;
        mesh->verts_scale[0] = 1.0f;
        mesh->verts_scale[1] = 1.0f;
        mesh->verts_scale[2] = 1.0f;


        for(uint32_t j = 0; j < skel_model->meshes[i].n_verts; j++) {
            // Write static fields:
            mesh->vert_rest_positions[j][0] = verts_pos[first_vert + j][0];
            mesh->vert_rest_positions[j][1] = verts_pos[first_vert + j][1];
            mesh->vert_rest_positions[j][2] = verts_pos[first_vert + j][2];
            mesh->vert_uvs[j][0] = verts_uv[first_vert + j][0];
            mesh->vert_uvs[j][1] = verts_uv[first_vert + j][1];
            mesh->vert_rest_normals[j][0] = verts_nor[first_vert + j][0];
            mesh->vert_rest_normals[j][1] = verts_nor[first_vert + j][1];
            mesh->vert_rest_normals[j][2] = verts_nor[first_vert + j][2];
            for(int k = 0; k < 4; k++) {
                mesh->vert_bone_weights[j * 4 + k] = verts_bone_weights[(first_vert + j) * 4 + k];
                mesh->vert_bone_idxs[j * 4 + k] = verts_bone_idxs[(first_vert + j) * 4 + k];
            }
        }

        // skel_model->meshes[i].n_tris = iqm_meshes[i].n_tris;
        mesh->n_tris = iqm_meshes[i].n_tris;
        mesh->tri_verts = (uint16_t*) malloc(sizeof(uint16_t) * 3 * mesh->n_tris);

        for(uint32_t j = 0; j < skel_model->meshes[i].n_tris; j++) {
            uint16_t vert_a = ((iqm_tri_t*)((uint8_t*) iqm_data + iqm_header->ofs_tris))[first_tri + j].vert_idxs[0] - first_vert;
            uint16_t vert_b = ((iqm_tri_t*)((uint8_t*) iqm_data + iqm_header->ofs_tris))[first_tri + j].vert_idxs[1] - first_vert;
            uint16_t vert_c = ((iqm_tri_t*)((uint8_t*) iqm_data + iqm_header->ofs_tris))[first_tri + j].vert_idxs[2] - first_vert;
            mesh->tri_verts[j*3 + 0] = vert_a;
            mesh->tri_verts[j*3 + 1] = vert_b;
            mesh->tri_verts[j*3 + 2] = vert_c;
        }

        mesh->vert16s = nullptr;
        mesh->vert8s = nullptr;
        mesh->n_submeshes = 0;
        mesh->submeshes = nullptr;

        // --
        // Split the mesh into one or more submeshes
        // --
        // IQM meshes can reference an arbitrary number of bones, but PSP can 
        // only reference 8 bones per mesh. So all meshes that reference more 
        // than 8 bones must be split into submeshes that reference 8 bones or
        // fewer each
        // --
        submesh_skeletal_mesh(mesh);
        Con_Printf("Done splitting mesh into submeshes\n");
        
        // --


        for(int submesh_idx = 0; submesh_idx < mesh->n_submeshes; submesh_idx++) {
            Con_Printf("Writing vert structs for mesh %d/%d submesh %d/%d\n", i+1, iqm_header->n_meshes, submesh_idx+1, mesh->n_submeshes);
            //  Cast float32 temp vertex data to vert8s / vert16s
            //  Remove triangle indices
            // TODO - Avoid building both vert8 and vert16 structs?
            write_skinning_vert_structs(&mesh->submeshes[submesh_idx], true, true);
            Con_Printf("Done writing vert structs for mesh %d/%d submesh %d/%d\n", i+1, iqm_header->n_meshes, submesh_idx+1, mesh->n_submeshes);
        }


        // Deallocate mesh's loading temporary structs (not used by drawing code)
        for(int submesh_idx = 0; submesh_idx < mesh->n_submeshes; submesh_idx++) {
            Con_Printf("Clearing temporary memory for mesh %d/%d submesh %d/%d\n", i+1, iqm_header->n_meshes, submesh_idx+1, mesh->n_submeshes);
            skeletal_mesh_t *submesh = &mesh->submeshes[submesh_idx];
            free_pointer_and_clear((void**) &submesh->vert_rest_positions);
            free_pointer_and_clear((void**) &submesh->vert_uvs);
            free_pointer_and_clear((void**) &submesh->vert_rest_normals);
            free_pointer_and_clear((void**) &submesh->vert_bone_weights); // NOTE - Unused by submeshes, but included for completeness
            free_pointer_and_clear((void**) &submesh->vert_bone_idxs);    // NOTE - Unused by submeshes, but included for completeness
            free_pointer_and_clear((void**) &submesh->vert_skinning_weights);
            free_pointer_and_clear((void**) &submesh->tri_verts);
            Con_Printf("Done clearing temporary memory for mesh %d/%d submesh %d/%d\n", i+1, iqm_header->n_meshes, submesh_idx+1, mesh->n_submeshes);
        }
        
        Con_Printf("Clearing temporary memory for mesh %d/%d\n", i+1, iqm_header->n_meshes);
        Con_Printf("\tClearing vert_rest_positions...\n", i+1, iqm_header->n_meshes);
        free_pointer_and_clear((void**) &mesh->vert_rest_positions);
        Con_Printf("\tClearing vert_rest_normals...\n", i+1, iqm_header->n_meshes);
        free_pointer_and_clear((void**) &mesh->vert_rest_normals);
        Con_Printf("\tClearing vert_uvs...\n", i+1, iqm_header->n_meshes);
        free_pointer_and_clear((void**) &mesh->vert_uvs);
        Con_Printf("\tClearing vert_bone_weights...\n", i+1, iqm_header->n_meshes);
        free_pointer_and_clear((void**) &mesh->vert_bone_weights);
        Con_Printf("\tClearing vert_bone_idxs...\n", i+1, iqm_header->n_meshes);
        free_pointer_and_clear((void**) &mesh->vert_bone_idxs);
        Con_Printf("\tClearing vert_skinning_weights...\n", i+1, iqm_header->n_meshes);
        free_pointer_and_clear((void**) &mesh->vert_skinning_weights);   // NOTE - Unused by meshes, but included for completeness
        Con_Printf("\tClearing tri_verts...\n", i+1, iqm_header->n_meshes);
        free_pointer_and_clear((void**) &mesh->tri_verts);
        Con_Printf("Done clearing temporary memory for mesh %d/%d\n", i+1, iqm_header->n_meshes);
    }

    // Deallocate iqm parsing buffers
    free(verts_pos);
    free(verts_uv);
    free(verts_nor);
    free(verts_bone_weights);
    free(verts_bone_idxs);


    // --------------------------------------------------
    // Parse bones
    // --------------------------------------------------
    Con_Printf("Parsing joints...\n");
    skel_model->n_bones = iqm_header->n_joints ? iqm_header->n_joints : iqm_header->n_poses;
    skel_model->bone_name = (char**) malloc(sizeof(char*) * skel_model->n_bones);
    skel_model->bone_parent_idx = (int16_t*) malloc(sizeof(int16_t) * skel_model->n_bones);
    skel_model->bone_rest_pos = (vec3_t*) malloc(sizeof(vec3_t) * skel_model->n_bones);
    skel_model->bone_rest_rot = (quat_t*) malloc(sizeof(quat_t) * skel_model->n_bones);
    skel_model->bone_rest_scale = (vec3_t*) malloc(sizeof(vec3_t) * skel_model->n_bones);

    const iqm_joint_quaternion_t *iqm_joints = (const iqm_joint_quaternion_t*) ((uint8_t*) iqm_data + iqm_header->ofs_joints);
    for(uint32_t i = 0; i < iqm_header->n_joints; i++) {
        const char *joint_name = (const char*) (((uint8_t*) iqm_data + iqm_header->ofs_text) + iqm_joints[i].name);
        skel_model->bone_name[i] = (char*) malloc(sizeof(char) * (strlen(joint_name) + 1));
        strcpy(skel_model->bone_name[i], joint_name);
        skel_model->bone_parent_idx[i] = iqm_joints[i].parent_joint_idx;
        skel_model->bone_rest_pos[i][0] = iqm_joints[i].translate[0];
        skel_model->bone_rest_pos[i][1] = iqm_joints[i].translate[1];
        skel_model->bone_rest_pos[i][2] = iqm_joints[i].translate[2];
        skel_model->bone_rest_rot[i][0] = iqm_joints[i].rotate[0];
        skel_model->bone_rest_rot[i][1] = iqm_joints[i].rotate[1];
        skel_model->bone_rest_rot[i][2] = iqm_joints[i].rotate[2];
        skel_model->bone_rest_rot[i][3] = iqm_joints[i].rotate[3];
        skel_model->bone_rest_scale[i][0] = iqm_joints[i].scale[0];
        skel_model->bone_rest_scale[i][1] = iqm_joints[i].scale[1];
        skel_model->bone_rest_scale[i][2] = iqm_joints[i].scale[2];
        // -- 
        Con_Printf("Parsed bone: %i, \"%s\"\n", i, skel_model->bone_name[i]);
        Con_Printf("\tPos: (%.2f, %.2f, %.2f)\n", skel_model->bone_rest_pos[i][0], skel_model->bone_rest_pos[i][1], skel_model->bone_rest_pos[i][2]);
        Con_Printf("\tRot: (%.2f, %.2f, %.2f, %.2f)\n", skel_model->bone_rest_rot[i][0], skel_model->bone_rest_rot[i][1], skel_model->bone_rest_rot[i][2], skel_model->bone_rest_rot[i][3]);
        Con_Printf("\tScale: (%.2f, %.2f, %.2f)\n", skel_model->bone_rest_scale[i][0], skel_model->bone_rest_scale[i][1], skel_model->bone_rest_scale[i][2]);
    }
    Con_Printf("\tParsed %d bones.\n", skel_model->n_bones);

    for(uint32_t i = 0; i < skel_model->n_bones; i++) {
        // i-th bone's parent index must be less than i
        if((int) i <= skel_model->bone_parent_idx[i]) {
            Con_Printf("Error: IQM file bones are sorted incorrectly. Bone %d is located before its parent bone %d.\n", i, skel_model->bone_parent_idx[i]);
            // TODO - Deallocate all allocated memory
            return nullptr;
        }
    }


    return skel_model;
}


// 
// Returns the total number of bytes required to store a skeletal_model_t
// including all data pointed to via pointers 
// 
uint32_t count_skel_model_n_bytes(skeletal_model_t *skel_model) {
    uint32_t skel_model_n_bytes = 0;
    skel_model_n_bytes += sizeof(skeletal_model_t);
    skel_model_n_bytes += sizeof(skeletal_mesh_t) * skel_model->n_meshes;
    for(int i = 0; i < skel_model->n_meshes; i++) {
        skeletal_mesh_t *mesh = &skel_model->meshes[i];
        skel_model_n_bytes += sizeof(skeletal_mesh_t) * mesh->n_submeshes;
        for(int j = 0; j < mesh->n_submeshes; j++) {
            skeletal_mesh_t *submesh = &mesh->submeshes[j];
            if(submesh->vert8s != nullptr) {
                skel_model_n_bytes += sizeof(skel_vertex_i8_t) * submesh->n_verts;
            }
            if(submesh->vert16s != nullptr) {
                skel_model_n_bytes += sizeof(skel_vertex_i16_t) * submesh->n_verts;
            }
        }
    }
    // -- bones --
    skel_model_n_bytes += sizeof(char*) * skel_model->n_bones;
    skel_model_n_bytes += sizeof(int16_t) * skel_model->n_bones;
    skel_model_n_bytes += sizeof(vec3_t) * skel_model->n_bones;
    skel_model_n_bytes += sizeof(quat_t) * skel_model->n_bones;
    skel_model_n_bytes += sizeof(vec3_t) * skel_model->n_bones;
    for(int i = 0; i < skel_model->n_bones; i++) {
        skel_model_n_bytes += sizeof(char) * (strlen(skel_model->bone_name[i]) + 1);
    }
    return skel_model_n_bytes;
}


//
// Copies the data contained in `skel_model` into `relocatable_skel_model` in 
// such a way that the model memory is fully relocatable. (i.e. all data is 
// laid out contiguously in memory, and pointers contain offsets relative to the 
// start of the memory block)
//
// NOTE - Assumes `relocatable_skel_model` is large enough to fully fit the skel_model's data
// NOTE - I'm horrified by this code too. There has got to be a better way to pull this off...
//
void make_skeletal_model_relocatable(skeletal_model_t *relocatable_skel_model, skeletal_model_t *skel_model) {
    // ------------–------------–------------–------------–------------–-------
    // Memcpy each piece of the skeletal model into the corresponding section
    // ------------–------------–------------–------------–------------–-------
    Con_Printf("Memcopying skeletal_model_t struct... ");
    uint8_t *ptr = (uint8_t*) relocatable_skel_model;

    // 1. Assign the pointer to the memory slot
    // 2. Copy the memory into the slot
    // 3. Increment the pointer to point to the next free slot
    // dest_data, src_data

    memcpy(relocatable_skel_model, skel_model, sizeof(skeletal_model_t));
    Con_Printf("DONE\n");
    ptr += sizeof(skeletal_model_t);
    Con_Printf("Memcopying skeletal_mesh_t array... ");
    relocatable_skel_model->meshes = (skeletal_mesh_t*) ptr;
    memcpy(relocatable_skel_model->meshes, skel_model->meshes, sizeof(skeletal_mesh_t) * skel_model->n_meshes);
    ptr += sizeof(skeletal_mesh_t) * skel_model->n_meshes;
    Con_Printf("DONE\n");

    for(int i = 0; i < skel_model->n_meshes; i++) {
        // Con_Printf("Memcopying vertex_t array for mesh %d ... ", i);
        // relocatable_skel_model->meshes[i].verts = (vertex_t*) ptr;
        // memcpy(relocatable_skel_model->meshes[i].verts, skel_model->meshes[i].verts, sizeof(vertex_t) * relocatable_skel_model->meshes[i].n_verts);
        // ptr += sizeof(vertex_t) * relocatable_skel_model->meshes[i].n_verts;
        // Con_Printf("DONE\n");

        Con_Printf("Memcopying submeshes skeletal_mesh_t array for mesh %d ... ", i);
        relocatable_skel_model->meshes[i].submeshes = (skeletal_mesh_t*) ptr;
        memcpy(relocatable_skel_model->meshes[i].submeshes, skel_model->meshes[i].submeshes, sizeof(skeletal_mesh_t) * skel_model->meshes[i].n_submeshes);
        ptr += sizeof(skeletal_mesh_t) * skel_model->meshes[i].n_submeshes;
        Con_Printf("DONE\n");

        for(int j = 0; j < skel_model->meshes[i].n_submeshes; j++) {
            if(skel_model->meshes[i].submeshes[j].vert8s != nullptr) {
                Con_Printf("Memcopying vert8s array for mesh %d submesh %d... ", i, j);
                relocatable_skel_model->meshes[i].submeshes[j].vert8s = (skel_vertex_i8_t*) ptr;
                memcpy(relocatable_skel_model->meshes[i].submeshes[j].vert8s, skel_model->meshes[i].submeshes[j].vert8s, sizeof(skel_vertex_i8_t) * skel_model->meshes[i].submeshes[j].n_verts);
                ptr += sizeof(skel_vertex_i8_t) * skel_model->meshes[i].submeshes[j].n_verts;
                Con_Printf("DONE\n");
            }
            if(skel_model->meshes[i].submeshes[j].vert16s != nullptr) {
                Con_Printf("Memcopying vert16s array for mesh %d submesh %d... ", i, j);
                relocatable_skel_model->meshes[i].submeshes[j].vert16s = (skel_vertex_i16_t*) ptr;
                memcpy(relocatable_skel_model->meshes[i].submeshes[j].vert16s, skel_model->meshes[i].submeshes[j].vert16s, sizeof(skel_vertex_i16_t) * skel_model->meshes[i].submeshes[j].n_verts);
                ptr += sizeof(skel_vertex_i16_t) * skel_model->meshes[i].submeshes[j].n_verts;
                Con_Printf("DONE\n");
            }
        }

        // Con_Printf("Memcopying tri_verts uint16_t array for mesh %d ... ", i);
        // relocatable_skel_model->meshes[i].tri_verts = (uint16_t*) ptr;
        // memcpy(relocatable_skel_model->meshes[i].tri_verts, skel_model->meshes[i].tri_verts, sizeof(uint16_t) * relocatable_skel_model->meshes[i].n_tris * 3);
        // ptr += sizeof(uint16_t) * skel_model->meshes[i].n_tris * 3;
        // Con_Printf("DONE\n");
    }
    // -- bones --
    Con_Printf("Memcopying bone_name char* array ... ");
    relocatable_skel_model->bone_name = (char**) ptr;
    memcpy(relocatable_skel_model->bone_name, skel_model->bone_name, sizeof(char*) * skel_model->n_bones);
    ptr += sizeof(char*) * skel_model->n_bones;
    Con_Printf("DONE\n");

    Con_Printf("Memcopying bone_parent_idx int16_t array ... ");
    relocatable_skel_model->bone_parent_idx = (int16_t*) ptr;
    memcpy(relocatable_skel_model->bone_parent_idx, skel_model->bone_parent_idx, sizeof(int16_t) * skel_model->n_bones);
    ptr += sizeof(int16_t) * skel_model->n_bones;
    Con_Printf("DONE\n");

    Con_Printf("Memcopying bone_rest_pos vec3_t array ... ");
    relocatable_skel_model->bone_rest_pos = (vec3_t*) ptr;
    memcpy(relocatable_skel_model->bone_rest_pos, skel_model->bone_rest_pos, sizeof(vec3_t) * skel_model->n_bones);
    ptr += sizeof(vec3_t) * skel_model->n_bones;
    Con_Printf("DONE\n");

    Con_Printf("Memcopying bone_rest_rot quat_t array ... ");
    relocatable_skel_model->bone_rest_rot = (quat_t*) ptr;
    memcpy(relocatable_skel_model->bone_rest_rot, skel_model->bone_rest_rot, sizeof(quat_t) * skel_model->n_bones);
    ptr += sizeof(quat_t) * skel_model->n_bones;
    Con_Printf("DONE\n");

    Con_Printf("Memcopying bone_rest_scale vec3_t array ... ");
    relocatable_skel_model->bone_rest_scale = (vec3_t*) ptr;
    memcpy(relocatable_skel_model->bone_rest_scale, skel_model->bone_rest_scale, sizeof(vec3_t) * skel_model->n_bones);
    ptr += sizeof(vec3_t) * skel_model->n_bones;
    Con_Printf("DONE\n");

    for(int i = 0; i < skel_model->n_bones; i++) {
        Con_Printf("Memcopying bone_name char array for bone %d ... ", i);
        relocatable_skel_model->bone_name[i] = (char*) ptr;
        memcpy(relocatable_skel_model->bone_name[i], skel_model->bone_name[i], sizeof(char) * (strlen(skel_model->bone_name[i]) + 1));
        ptr += sizeof(char) * (strlen(skel_model->bone_name[i]) + 1);
        Con_Printf("DONE\n");
    }
    // ------------–------------–------------–------------–------------–-------

    // ------------–------------–------------–------------–------------–-------
    // Clean up all pointers to be relative to model start location in memory
    // ------------–------------–------------–------------–------------–-------
    for(int i = 0; i < skel_model->n_bones; i++) {
        Con_Printf("Shifting bone_name char array for bone %d ... ", i);
        relocatable_skel_model->bone_name[i] = (char*) ((uint8_t*)(relocatable_skel_model->bone_name[i]) - (uint8_t*)relocatable_skel_model);
        Con_Printf("DONE\n");
    }
    Con_Printf("Shifting bone_name char* array ... ");
    relocatable_skel_model->bone_name = (char**) ((uint8_t*)(relocatable_skel_model->bone_name) - (uint8_t*)relocatable_skel_model);
    Con_Printf("DONE\n");
    Con_Printf("Shifting bone_parent_idx int16_t array ... ");
    relocatable_skel_model->bone_parent_idx = (int16_t*) ((uint8_t*)(relocatable_skel_model->bone_parent_idx) - (uint8_t*)relocatable_skel_model);
    Con_Printf("DONE\n");
    Con_Printf("Shifting bone_rest_pos vec3_t array ... ");
    relocatable_skel_model->bone_rest_pos = (vec3_t*) ((uint8_t*)(relocatable_skel_model->bone_rest_pos) - (uint8_t*)relocatable_skel_model);
    Con_Printf("DONE\n");
    Con_Printf("Shifting bone_rest_rot quat_t array ... ");
    relocatable_skel_model->bone_rest_rot = (quat_t*) ((uint8_t*)(relocatable_skel_model->bone_rest_rot) - (uint8_t*)relocatable_skel_model);
    Con_Printf("DONE\n");
    Con_Printf("Shifting bone_rest_scale vec3_t array ... ");
    relocatable_skel_model->bone_rest_scale = (vec3_t*) ((uint8_t*)(relocatable_skel_model->bone_rest_scale) - (uint8_t*)relocatable_skel_model);
    Con_Printf("DONE\n");

    for(int i = 0; i < skel_model->n_meshes; i++) {
        for(int j = 0; j < skel_model->meshes[i].n_submeshes; j++) {
            if(skel_model->meshes[i].submeshes[j].vert8s != nullptr) {
                Con_Printf("Shifting vert8s array pointer for mesh %d submesh %d ... ", i, j);
                relocatable_skel_model->meshes[i].submeshes[j].vert8s = (skel_vertex_i8_t*) ((uint8_t*)(relocatable_skel_model->meshes[i].submeshes[j].vert8s) - (uint8_t*)relocatable_skel_model);
                Con_Printf("DONE\n");
            }
            if(skel_model->meshes[i].submeshes[j].vert16s != nullptr) {
                Con_Printf("Shifting vert16s array pointer for mesh %d submesh %d ... ", i, j);
                relocatable_skel_model->meshes[i].submeshes[j].vert16s = (skel_vertex_i16_t*) ((uint8_t*)(relocatable_skel_model->meshes[i].submeshes[j].vert16s) - (uint8_t*)relocatable_skel_model);
                Con_Printf("DONE\n");
            }
        }

        Con_Printf("Shifting submesh array pointer for mesh %d ... ", i);
        relocatable_skel_model->meshes[i].submeshes = (skeletal_mesh_t*) ((uint8_t*)(relocatable_skel_model->meshes[i].submeshes) - (uint8_t*)relocatable_skel_model);
        Con_Printf("DONE\n");
        // Con_Printf("Shifting vertex_t array pointer for mesh %d ... ", i);
        // relocatable_skel_model->meshes[i].verts = (vertex_t*) ((uint8_t*)(relocatable_skel_model->meshes[i].verts) - (uint8_t*)relocatable_skel_model);
        // Con_Printf("DONE\n");
        // Con_Printf("Shifting tri_verts uint16_t array pointer for mesh %d ... ", i);
        // relocatable_skel_model->meshes[i].tri_verts = (uint16_t*) ((uint8_t*)(relocatable_skel_model->meshes[i].tri_verts) - (uint8_t*)relocatable_skel_model);
        // Con_Printf("DONE\n");
    }
    Con_Printf("Shifting skeletal_mesh_t array pointer... ");
    relocatable_skel_model->meshes = (skeletal_mesh_t*) ((uint8_t*)(relocatable_skel_model->meshes) - (uint8_t*)relocatable_skel_model);
    Con_Printf("DONE\n");
    // ------------–------------–------------–------------–------------–-------
}

//
// Completely deallocates a skeletal_model_t struct, pointers and all.
// NOTE - This should not be used on a relocatable skeletal_model_t object.
//
void free_skeletal_model(skeletal_model_t *skel_model) {
    for(int i = 0; i < skel_model->n_bones; i++) {
        free(skel_model->bone_name[i]);
    }
    free(skel_model->bone_name);
    free(skel_model->bone_parent_idx);
    free(skel_model->bone_rest_pos);
    free(skel_model->bone_rest_rot);
    free(skel_model->bone_rest_scale);
    for(int i = 0; i < skel_model->n_meshes; i++) {
        for(int j = 0; j < skel_model->meshes[i].n_submeshes; j++) {
            // These submesh struct members are expected to be nullptr:
            free_pointer_and_clear((void**) &skel_model->meshes[i].submeshes[j].vert_rest_positions);
            free_pointer_and_clear((void**) &skel_model->meshes[i].submeshes[j].vert_uvs);
            free_pointer_and_clear((void**) &skel_model->meshes[i].submeshes[j].vert_rest_normals);
            free_pointer_and_clear((void**) &skel_model->meshes[i].submeshes[j].vert_bone_weights);
            free_pointer_and_clear((void**) &skel_model->meshes[i].submeshes[j].vert_bone_idxs);
            free_pointer_and_clear((void**) &skel_model->meshes[i].submeshes[j].vert_skinning_weights);
            free_pointer_and_clear((void**) &skel_model->meshes[i].submeshes[j].tri_verts);
            free_pointer_and_clear((void**) &skel_model->meshes[i].submeshes[j].submeshes);

            // These submesh struct members are expected to be allocated:
            free_pointer_and_clear((void**) &skel_model->meshes[i].submeshes[j].vert8s);     // Only free if not nullptr
            free_pointer_and_clear((void**) &skel_model->meshes[i].submeshes[j].vert16s);    // Only free if not nullptr
        }
        
        // These mesh struct members are expected to be nullptr:
        free_pointer_and_clear((void**) &skel_model->meshes[i].vert_rest_positions);
        free_pointer_and_clear((void**) &skel_model->meshes[i].vert_uvs);
        free_pointer_and_clear((void**) &skel_model->meshes[i].vert_rest_normals);
        free_pointer_and_clear((void**) &skel_model->meshes[i].vert_bone_weights);
        free_pointer_and_clear((void**) &skel_model->meshes[i].vert_bone_idxs);
        free_pointer_and_clear((void**) &skel_model->meshes[i].vert_skinning_weights);
        free_pointer_and_clear((void**) &skel_model->meshes[i].tri_verts);
        free_pointer_and_clear((void**) &skel_model->meshes[i].vert16s);
        free_pointer_and_clear((void**) &skel_model->meshes[i].vert8s);

        // These mesh struct members are expected to be allocated:
        free_pointer_and_clear((void**) &skel_model->meshes[i].submeshes);
    }
    free(skel_model->meshes);
    free(skel_model);
}


/*
=================
Mod_LoadIQMModel
=================
*/
extern char loadname[];
void Mod_LoadIQMModel (model_t *model, void *buffer) {

    skeletal_model_t *skel_model = load_iqm_file(buffer);

    // ------------------------------------------------------------------------
    // Debug - printing the skel_model contents
    // ------------------------------------------------------------------------
    Con_Printf("------------------------------------------------------------\n");
    Con_Printf("Debug printing skeletal model\n");
    Con_Printf("------------------------------------------------------------\n");
    Con_Printf("skel_model->n_meshes = %d\n", skel_model->n_meshes);
    for(int i = 0; i < skel_model->n_meshes; i++) {
        Con_Printf("---------------------\n");
        Con_Printf("skel_model->meshes[%d].n_verts = %d\n", i, skel_model->meshes[i].n_verts);
        Con_Printf("skel_model->meshes[%d].n_tris = %d\n", i, skel_model->meshes[i].n_tris);
        Con_Printf("skel_model->meshes[%d].vert_rest_positions = %d\n", i, skel_model->meshes[i].vert_rest_positions);
        Con_Printf("skel_model->meshes[%d].vert_uvs = %d\n", i, skel_model->meshes[i].vert_uvs);
        Con_Printf("skel_model->meshes[%d].vert_rest_normals = %d\n", i, skel_model->meshes[i].vert_rest_normals);
        Con_Printf("skel_model->meshes[%d].vert_bone_weights = %d\n", i, skel_model->meshes[i].vert_bone_weights);
        Con_Printf("skel_model->meshes[%d].vert_bone_idxs = %d\n", i, skel_model->meshes[i].vert_bone_idxs);
        Con_Printf("skel_model->meshes[%d].vert_skinning_weights = %d\n", i, skel_model->meshes[i].vert_skinning_weights);
        Con_Printf("skel_model->meshes[%d].tri_verts = %d\n", i, skel_model->meshes[i].tri_verts);
        Con_Printf("skel_model->meshes[%d].vert16s = %d\n", i, skel_model->meshes[i].vert16s);
        Con_Printf("skel_model->meshes[%d].vert8s = %d\n", i, skel_model->meshes[i].vert8s);
        Con_Printf("skel_model->meshes[%d].n_skinning_bones = %d\n", i, skel_model->meshes[i].n_skinning_bones);
        // --
        Con_Printf("\tskel_model->meshes[%d].skinning_bone_idxs = [", i);
        for(int k = 0; k < 8; k++) {
            Con_Printf("%d, ", skel_model->meshes[i].skinning_bone_idxs[k]);
        }
        Con_Printf("]\n");
        // --
        Con_Printf("\tskel_model->meshes[%d].verts_ofs = [", i);
        for(int k = 0; k < 3; k++) {
            Con_Printf("%f, ", skel_model->meshes[i].verts_ofs[k]);
        }
        Con_Printf("]\n");
        // --
        Con_Printf("\tskel_model->meshes[%d].verts_scale = [", i);
        for(int k = 0; k < 3; k++) {
            Con_Printf("%f, ", skel_model->meshes[i].verts_scale[k]);
        }
        Con_Printf("]\n");
        // --
        Con_Printf("skel_model->meshes[%d].n_submeshes = %d\n", i, skel_model->meshes[i].n_submeshes);
        Con_Printf("skel_model->meshes[%d].submeshes = %d\n", i, skel_model->meshes[i].submeshes);

        for(int j = 0; j < skel_model->meshes[i].n_submeshes; j++) {
            Con_Printf("\t----------\n");
            Con_Printf("\tskel_model->meshes[%d].submeshes[%d].n_verts = %d\n", i, j, skel_model->meshes[i].submeshes[j].n_verts);
            Con_Printf("\tskel_model->meshes[%d].submeshes[%d].n_tris = %d\n", i, j, skel_model->meshes[i].submeshes[j].n_tris);
            Con_Printf("\tskel_model->meshes[%d].submeshes[%d].vert_rest_positions = %d\n", i, j, skel_model->meshes[i].submeshes[j].vert_rest_positions);
            Con_Printf("\tskel_model->meshes[%d].submeshes[%d].vert_uvs = %d\n", i, j, skel_model->meshes[i].submeshes[j].vert_uvs);
            Con_Printf("\tskel_model->meshes[%d].submeshes[%d].vert_rest_normals = %d\n", i, j, skel_model->meshes[i].submeshes[j].vert_rest_normals);
            Con_Printf("\tskel_model->meshes[%d].submeshes[%d].vert_bone_weights = %d\n", i, j, skel_model->meshes[i].submeshes[j].vert_bone_weights);
            Con_Printf("\tskel_model->meshes[%d].submeshes[%d].vert_bone_idxs = %d\n", i, j, skel_model->meshes[i].submeshes[j].vert_bone_idxs);
            Con_Printf("\tskel_model->meshes[%d].submeshes[%d].vert_skinning_weights = %d\n", i, j, skel_model->meshes[i].submeshes[j].vert_skinning_weights);
            Con_Printf("\tskel_model->meshes[%d].submeshes[%d].tri_verts = %d\n", i, j, skel_model->meshes[i].submeshes[j].tri_verts);
            Con_Printf("\tskel_model->meshes[%d].submeshes[%d].vert16s = %d\n", i, j, skel_model->meshes[i].submeshes[j].vert16s);
            Con_Printf("\tskel_model->meshes[%d].submeshes[%d].vert8s = %d\n", i, j, skel_model->meshes[i].submeshes[j].vert8s);
            Con_Printf("\tskel_model->meshes[%d].submeshes[%d].n_skinning_bones = %d\n", i, j, skel_model->meshes[i].submeshes[j].n_skinning_bones);
            // --
            Con_Printf("\tskel_model->meshes[%d].submeshes[%d].skinning_bone_idxs = [", i, j);
            for(int k = 0; k < 8; k++) {
                Con_Printf("%d, ", skel_model->meshes[i].submeshes[j].skinning_bone_idxs[k]);
            }
            Con_Printf("]\n");
            // --
            Con_Printf("\tskel_model->meshes[%d].submeshes[%d].verts_ofs = [", i, j);
            for(int k = 0; k < 3; k++) {
                Con_Printf("%f, ", skel_model->meshes[i].submeshes[j].verts_ofs[k]);
            }
            Con_Printf("]\n");
            // --
            Con_Printf("\tskel_model->meshes[%d].submeshes[%d].verts_scale = [", i, j);
            for(int k = 0; k < 3; k++) {
                Con_Printf("%f, ", skel_model->meshes[i].submeshes[j].verts_scale[k]);
            }
            Con_Printf("]\n");
            // --
            Con_Printf("\tskel_model->meshes[%d].submeshes[%d].n_submeshes = %d\n", i, j, skel_model->meshes[i].submeshes[j].n_submeshes);
            Con_Printf("\tskel_model->meshes[%d].submeshes[%d].submeshes = %d\n", i, j, skel_model->meshes[i].submeshes[j].submeshes);
            Con_Printf("\t----------\n");
        }
        Con_Printf("---------------------\n");
    }
    Con_Printf("------------------------------------------------------------\n");
    // ------------------------------------------------------------------------


    // TODO - Need to update this function to work with latest version of skeletal_model_t
    uint32_t skel_model_n_bytes = count_skel_model_n_bytes(skel_model);
    // skeletal_model_t *relocatable_skel_model = (skeletal_model_t*) malloc(skel_model_n_bytes); // TODO - Pad to 2-byte alignment?
    skeletal_model_t *relocatable_skel_model = (skeletal_model_t*) Hunk_AllocName(skel_model_n_bytes, loadname); // TODO - Pad to 2-byte alignment?
    // TODO - Need to update this function to work with latest version of skeletal_model_t
    // make_skeletal_model_relocatable(relocatable_skel_model, skel_model);
    // model->type = mod_iqm;
    // Con_Printf("Copying final skeletal model struct... ");
    // if (!model->cache.data)
    //     Cache_Alloc(&model->cache, skel_model_n_bytes, loadname);
    // if (!model->cache.data)
    //     return;

    // memcpy_vfpu(model->cache.data, (void*) relocatable_skel_model, skel_model_n_bytes);
    // Con_Printf("DONE\n");

    // Con_Printf("About to free temp skeletal model struct...");
    // // TODO - Need to update this function to work with latest version of skeletal_model_t
    // free_skeletal_model(skel_model);
    // skel_model = nullptr;
    // Con_Printf("DONE\n");

    // TODO - Need to understand where exactly the model is being cached...
    // TODO - Need to understand where exactly where to stash the model pointer...

}



void R_DrawIQMModel(entity_t *ent) {
    Con_Printf("IQM model draw!\n");

    sceGumPushMatrix();
    R_BlendedRotateForEntity(ent, 0, ent->scale);

    sceGuDisable(GU_TEXTURE_2D);
    sceGuColor(0x000000);

    skeletal_model_t *skel_model = (skeletal_model_t*) Mod_Extradata(ent->model);
    // FIXME - Update this to draw meshes and submeshes
    // for(int i = 0; i < skel_model->n_meshes; i++) {
    //     skeletal_mesh_t *mesh = &((skeletal_mesh_t*) ((uint8_t*)skel_model + (int)skel_model->meshes))[i];
    //     // FIXME
    //     vertex_t *mesh_verts = (vertex_t*) ((uint8_t*) skel_model + (int) mesh->verts);
    //     uint16_t *mesh_tri_verts = (uint16_t*) ((uint8_t*) skel_model + (int) mesh->tri_verts);
    //     sceGumDrawArray(GU_TRIANGLES,GU_INDEX_16BIT|GU_TEXTURE_32BITF|GU_NORMAL_32BITF|GU_VERTEX_32BITF|GU_TRANSFORM_3D, mesh->n_tris * 3, mesh_tri_verts, mesh_verts);
    // }

    sceGuEnable(GU_TEXTURE_2D);


    // Draw bones
    // Con_Printf("------------------------------\n");
    char **bone_names = (char**) ((uint8_t*) skel_model + (int) skel_model->bone_name);
    vec3_t *bone_rest_pos = (vec3_t*) ((uint8_t*)skel_model + (int)skel_model->bone_rest_pos);
    quat_t *bone_rest_rot = (quat_t*) ((uint8_t*)skel_model + (int)skel_model->bone_rest_rot);
    vec3_t *bone_rest_scale = (vec3_t*) ((uint8_t*)skel_model + (int)skel_model->bone_rest_scale);
    int16_t *bone_parent_idx = (int16_t*) ((uint8_t*)skel_model + (int)skel_model->bone_parent_idx);

    // TODO - Move this into the model struct...?
    mat3x4_t bone_rest_transforms[25];
    mat3x4_t inv_bone_rest_transforms[25];

    for(int i = 0; i < skel_model->n_bones; i++) {
        Matrix3x4_scale_rotate_translate( bone_rest_transforms[i], bone_rest_scale[i], bone_rest_rot[i], bone_rest_pos[i]);

        if(bone_parent_idx[i] >= 0) {
            mat3x4_t temp;
            Matrix3x4_ConcatTransforms( temp, bone_rest_transforms[bone_parent_idx[i]], bone_rest_transforms[i]);
            Matrix3x4_Copy(bone_rest_transforms[i], temp);
        }
    }
    for(int i = 0; i < skel_model->n_bones; i++) {
        Matrix3x4_Invert_Simple( inv_bone_rest_transforms[i], bone_rest_transforms[i]);
    }

    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_TEXTURE_2D);
    for(int i = 0; i < skel_model->n_bones; i++) {
        float line_verts_x[6] = {0,0,0,     1,0,0}; // Verts for x-axis
        float line_verts_y[6] = {0,0,0,     0,1,0}; // Verts for y-axis
        float line_verts_z[6] = {0,0,0,     0,0,1}; // Verts for z-axis
        ScePspFMatrix4 bone_mat;
        bone_mat.x.x = bone_rest_transforms[i][0][0];   bone_mat.y.x = bone_rest_transforms[i][0][1];   bone_mat.z.x = bone_rest_transforms[i][0][2];   bone_mat.w.x = bone_rest_transforms[i][0][3];
        bone_mat.x.y = bone_rest_transforms[i][1][0];   bone_mat.y.y = bone_rest_transforms[i][1][1];   bone_mat.z.y = bone_rest_transforms[i][1][2];   bone_mat.w.y = bone_rest_transforms[i][1][3];
        bone_mat.x.z = bone_rest_transforms[i][2][0];   bone_mat.y.z = bone_rest_transforms[i][2][1];   bone_mat.z.z = bone_rest_transforms[i][2][2];   bone_mat.w.z = bone_rest_transforms[i][2][3];
        bone_mat.x.w = 0.0f;                bone_mat.y.w = 0.0f;                bone_mat.z.w = 0.0f;                bone_mat.w.w = 1.0f;
        sceGumPushMatrix();
        sceGumMultMatrix(&bone_mat);
        sceGuColor(0x0000ff); // red
        sceGumDrawArray(GU_LINES,GU_VERTEX_32BITF|GU_TRANSFORM_3D, 2, nullptr, line_verts_x);
        sceGuColor(0x00ff00); // green
        sceGumDrawArray(GU_LINES,GU_VERTEX_32BITF|GU_TRANSFORM_3D, 2, nullptr, line_verts_y);
        sceGuColor(0xff0000); // blue
        sceGumDrawArray(GU_LINES,GU_VERTEX_32BITF|GU_TRANSFORM_3D, 2, nullptr, line_verts_z);
        sceGumPopMatrix();
    }
    sceGuEnable(GU_DEPTH_TEST);
    sceGuEnable(GU_TEXTURE_2D);


    // for(int i = 0; i < skel_model->n_bones; i++) {
    //     char *bone_name = (char*)((uint8_t*) skel_model + (int) bone_names[i]);
    //     // Con_Printf("Drawing bone: %i, \"%s\"\n", i, bone_name);
    //     // Con_Printf("\tParent bone: %d\n", bone_parent_idx[i]);
    //     // Con_Printf("\tPos: (%.2f, %.2f, %.2f)\n", bone_rest_pos[i][0], bone_rest_pos[i][1], bone_rest_pos[i][2]);
    //     // Con_Printf("\tRot: (%.2f, %.2f, %.2f, %.2f)\n", bone_rest_rot[i][0], bone_rest_rot[i][1], bone_rest_rot[i][2], bone_rest_rot[i][3]);
    //     // Con_Printf("\tScale: (%.2f, %.2f, %.2f)\n", bone_rest_scale[i][0], bone_rest_scale[i][1], bone_rest_scale[i][2]);
    //    // TODO - Draw a line from ... wait what? I guess we should just print it for now...
    // }
    sceGumPopMatrix();
}

