# Advanced Knee MRI AI Agent Test Tasks

This document defines 10 advanced, clinically realistic scenarios to evaluate the reasoning, planning, and multi-tool execution capabilities of the ITK-SNAP AI Agent.

---

### Task 1: Joint Effusion Fluid Volume Analysis
*   **Clinical Goal**: Locate and measure the volume of joint effusion (excess fluid) which shows up as highly bright signals on T2-weighted MRI scans.
*   **Prompt**: 
    > *"Locate the bright fluid-like signal regions on the T2-weighted knee MRI. Perform a threshold-based segmentation for voxel intensities in the upper range [800, 1800] into Label 1, name this structure 'Joint Effusion', fill any internal 3D holes in the segmentation, and compute the total fluid volume in mL."*
*   **Chained Tools Expected**:
    1. `threshold_segment` (label=1, lower=800, upper=1800, name="Joint Effusion")
    2. `fill_holes_label` (label=1)
    3. `measure_volume` (label=1)

---

### Task 2: Cartilage Refinement & Cleanup
*   **Clinical Goal**: Clean up noise and isolated misclassified voxels on a cartilage labeling mask.
*   **Prompt**: 
    > *"We have an existing cartilage segmentation in Label 3 named 'Femoral Cartilage'. Smooth the boundaries of this label using a physical Gaussian sigma of 1.2mm, erase any stray isolated voxels by clearing anything below a 10-voxel connected threshold, and save the updated segmentation to its current file path."*
*   **Chained Tools Expected**:
    1. `smooth_labels` (label=3, sigma_mm=1.2)
    2. `clear_label` (erasing outliers or applying filtering)
    3. `save_segmentation`

---

### Task 3: Meniscal Tear Distance Localization
*   **Clinical Goal**: Calculate the physical distance from a suspected posterior horn meniscus tear to the tibial plateau.
*   **Prompt**: 
    > *"Move the crosshairs to the meniscus tear at voxel coordinates [118, 142, 75]. Convert this cursor location to physical world coordinates (in mm). Then, measure the 3D physical distance between this point and a reference point on the tibial plateau at voxel [118, 142, 60]."*
*   **Chained Tools Expected**:
    1. `move_cursor` (x=118, y=142, z=75)
    2. `voxel_to_world` (x_vox=118, y_vox=142, z_vox=75)
    3. `measure_distance` (x1=118, y1=142, z1=75, x2=118, y2=142, z2=60)

---

### Task 4: Patellofemoral Follow-up Image Alignment (Coregistration)
*   **Clinical Goal**: Load a follow-up knee scan as an overlay and align it to the baseline scan for longitudinal tracking.
*   **Prompt**: 
    > *"Load the follow-up knee scan overlay located at 'D:/itksnap-build/followup_knee.nii.gz'. Run an automatic multi-resolution affine registration to align the overlay to the main baseline image, and set the overlay opacity to 50% for visual verification."*
*   **Chained Tools Expected**:
    1. `load_overlay` (path="D:/itksnap-build/followup_knee.nii.gz")
    2. `register_images` (transform="affine", metric="nmi")
    3. `set_overlay_opacity` (overlay_index=0, opacity=0.5)

---

### Task 5: Multichannel T1/T2 Visual Fusion Configuration
*   **Clinical Goal**: Configure the layout to view T1 and T2 sequences side by side with custom lookup tables.
*   **Prompt**: 
    > *"Apply a 'Grayscale' colormap to the main T1 image and apply a 'Jet' color map to the loaded T2 overlay. Change the layout view to Axial-only and automatically adjust the window/level contrast of both layers to maximize soft tissue detail."*
*   **Chained Tools Expected**:
    1. `set_colormap` (preset="Grayscale", target="main")
    2. `set_colormap` (preset="Jet", target="overlay")
    3. `set_layout` (layout="axial")
    4. `auto_window_level`

---

### Task 6: Subchondral Bone Marrow Edema Profiling
*   **Clinical Goal**: Analyze signal intensity variations in subchondral bone layers to assess for bone marrow edema.
*   **Prompt**: 
    > *"Focus the view on the bone segmentation (Label 2). Compute the voxel intensity histogram distribution within this region using 64 bins, and extract the mean and standard deviation of bone intensities to check for signal deviations."*
*   **Chained Tools Expected**:
    1. `focus_label` (label=2)
    2. `get_intensity_histogram` (bins=64)
    3. `get_label_stats` (label=2)

---

### Task 7: Anterior Cruciate Ligament (ACL) Active Contour Evolution
*   **Clinical Goal**: Seed and grow a 3D active contour around the ACL bundle.
*   **Prompt**: 
    > *"We want to segment the ACL. Move the cursor to [128, 130, 85]. Enter active contour mode with a seed bubble of 3.0mm radius at this location. Set the intensity threshold bounds to [300, 750] for the ligament structure, run 80 evolution iterations to grow the shape, and update the 3D surface mesh."*
*   **Chained Tools Expected**:
    1. `move_cursor` (x=128, y=130, z=85)
    2. `active_contour_segment` (seed_x=128, seed_y=130, seed_z=85, seed_radius_mm=3.0, lower=300, upper=750, iterations=80)
    3. `update_3d_mesh`

---

### Task 8: Spatial Geometry & Orientation Audit
*   **Clinical Goal**: Audit the coordinate matrix, verify scan tilt, and check physical dimensions.
*   **Prompt**: 
    > *"Check if the loaded knee MRI scan contains oblique (tilted) slices. Return the 3-letter anatomical RAI orientation code and check the physical spacing between slices to ensure high-resolution isotropic quality."*
*   **Chained Tools Expected**:
    1. `get_image_header` (returns spacing, orientation)
    2. `get_rai_orientation`

---

### Task 9: Multi-structure Protected Segmentation Drawing
*   **Clinical Goal**: Edit the tibial bone border without overwriting the adjacent femoral cartilage.
*   **Prompt**: 
    > *"Switch the active drawing tool to the Paintbrush and set the brush size to 6 voxels. Set the active drawing label to 4 (Tibia). Configure the draw-over filter so that we only paint over clear voxels (Label 0), protecting Label 3 (Cartilage) from accidental overwriting."*
*   **Chained Tools Expected**:
    1. `set_toolbar_mode` (mode="paintbrush")
    2. `set_paintbrush_size` (size=6)
    3. `set_active_label` (label=4)
    4. `set_draw_over_label` (filter="clear")

---

### Task 10: Automatic Exporting & Mesh Pipeline
*   **Clinical Goal**: Reconstruct and export the segmented tibial structure for surgical planning.
*   **Prompt**: 
    > *"Focus the view on Label 4. Regenerate its 3D surface mesh, export the 3D mesh as a VTK file to 'D:/itksnap-build/tibia_mesh.vtk', save a coronal slice image of the tibia to 'D:/itksnap-build/tibia_coronal.png', and save the current workspace."*
*   **Chained Tools Expected**:
    1. `focus_label` (label=4)
    2. `update_3d_mesh`
    3. `export_3d_mesh` (label=4, path="D:/itksnap-build/tibia_mesh.vtk")
    4. `export_slice` (direction="coronal", path="D:/itksnap-build/tibia_coronal.png")
    5. `save_workspace` (path="D:/itksnap-build/knee_study.itksnap")
