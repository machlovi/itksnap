# Eval Prompts (187 scenarios)

Phantom: background=40, organ=100, nodule=220 (128^3).

| id | diff | category | prompt | expected tools |
|---|---|---|---|---|
| S001 | 1 | tool:load_image | load the image at D:/itksnap-build/phantom.nii.gz | load_image |
| S002 | 1 | tool:load_image | open the scan D:/itksnap-build/phantom.nii.gz | load_image |
| S003 | 1 | tool:load_image | I want to work on the volume, load D:/itksnap-build/phantom.nii.gz | load_image |
| S004 | 1 | tool:load_image | bring up the main image from D:/itksnap-build/phantom.nii.gz | load_image |
| S005 | 1 | tool:load_image | open the file D:/itksnap-build/phantom.nii.gz | load_image |
| S006 | 1 | tool:get_scene_overview | what's currently loaded? | get_scene_overview |
| S007 | 1 | tool:get_scene_overview | give me an overview of the scene | get_scene_overview |
| S008 | 1 | tool:get_scene_overview | describe the current state of the viewer | get_scene_overview |
| S009 | 1 | tool:get_scene_overview | what am I looking at right now? | get_scene_overview |
| S010 | 1 | tool:get_scene_overview | summarize what's in itk-snap at the moment | get_scene_overview |
| S011 | 2 | tool:threshold_segment | segment everything brighter than 200 as label 1 | threshold_segment |
| S012 | 2 | tool:threshold_segment | outline the bright nodule for me | threshold_segment |
| S013 | 3 | tool:threshold_segment | segment the organ tissue around intensity 100 into label 2 | threshold_segment |
| S014 | 3 | tool:threshold_segment | threshold between 40 and 60 and call it background | threshold_segment |
| S015 | 3 | tool:threshold_segment | mark all voxels with value 220 exactly as a lesion in label 3 | threshold_segment |
| S016 | 2 | tool:measure_volume | how big is label 1? | measure_volume |
| S017 | 2 | tool:measure_volume | what's the volume of the nodule in mL? | measure_volume |
| S018 | 2 | tool:measure_volume | measure label 2 | measure_volume |
| S019 | 2 | tool:measure_volume | give me the size of the segmented organ | measure_volume |
| S020 | 2 | tool:measure_volume | report the volume of structure 1 | measure_volume |
| S021 | 2 | tool:measure_all_labels | measure every label | measure_all_labels |
| S022 | 2 | tool:measure_all_labels | give me all the volumes | measure_all_labels |
| S023 | 2 | tool:measure_all_labels | what are the sizes of all structures? | measure_all_labels |
| S024 | 2 | tool:measure_all_labels | report volumes for each segmentation | measure_all_labels |
| S025 | 2 | tool:measure_all_labels | tabulate all label volumes | measure_all_labels |
| S026 | 2 | tool:count_voxels | how many voxels are in label 1? | count_voxels |
| S027 | 2 | tool:count_voxels | count the voxels of the nodule | count_voxels |
| S028 | 2 | tool:count_voxels | voxel count for structure 1 | count_voxels |
| S029 | 2 | tool:count_voxels | how many pixels did label 1 capture? | count_voxels |
| S030 | 2 | tool:count_voxels | tell me the raw voxel count of label 1 | count_voxels |
| S031 | 2 | tool:clear_segmentation | erase the whole segmentation | clear_segmentation |
| S032 | 2 | tool:clear_segmentation | start the segmentation over from scratch | clear_segmentation |
| S033 | 2 | tool:clear_segmentation | wipe all labels | clear_segmentation |
| S034 | 2 | tool:clear_segmentation | clear everything I've segmented | clear_segmentation |
| S035 | 2 | tool:clear_segmentation | reset the segmentation completely | clear_segmentation |
| S036 | 3 | tool:clear_label | erase label 1 only | clear_label |
| S037 | 3 | tool:clear_label | remove the nodule segmentation but keep the organ | clear_label |
| S038 | 3 | tool:clear_label | delete just label 1's voxels | clear_label |
| S039 | 3 | tool:clear_label | get rid of structure 1 | clear_label |
| S040 | 3 | tool:clear_label | clear label 1 | clear_label |
| S041 | 4 | tool:replace_label | move everything in label 1 into label 2 | replace_label |
| S042 | 4 | tool:replace_label | merge label 1 into label 2 | replace_label |
| S043 | 4 | tool:replace_label | relabel structure 1 as label 2 | replace_label |
| S044 | 4 | tool:replace_label | change label 1 voxels to label 2 | replace_label |
| S045 | 4 | tool:replace_label | reassign label 1 to be label 2 | replace_label |
| S046 | 2 | tool:set_active_label | make label 3 the active drawing label | set_active_label |
| S047 | 2 | tool:set_active_label | switch the current label to 3 | set_active_label |
| S048 | 2 | tool:set_active_label | set the drawing color to label 3 | set_active_label |
| S049 | 2 | tool:set_active_label | I want to paint with label 3 next | set_active_label |
| S050 | 2 | tool:set_active_label | activate label 3 for drawing | set_active_label |
| S051 | 2 | tool:rename_label | rename label 1 to liver | rename_label |
| S052 | 2 | tool:rename_label | call label 1 'tumor' | rename_label |
| S053 | 2 | tool:rename_label | change the name of label 1 to kidney | rename_label |
| S054 | 2 | tool:rename_label | label 1 should be named spleen | rename_label |
| S055 | 2 | tool:rename_label | give label 1 the name 'lesion' | rename_label |
| S056 | 2 | tool:set_label_color | make label 1 red | set_label_color |
| S057 | 2 | tool:set_label_color | color label 1 with RGB 0 255 0 | set_label_color |
| S058 | 2 | tool:set_label_color | turn label 1 blue | set_label_color |
| S059 | 2 | tool:set_label_color | set the color of label 1 to bright green | set_label_color |
| S060 | 2 | tool:set_label_color | recolor label 1 to 255 255 0 | set_label_color |
| S061 | 2 | tool:move_cursor | move the crosshair to voxel 10, 20, 30 | move_cursor |
| S062 | 2 | tool:move_cursor | jump the cursor to 64 64 64 | move_cursor |
| S063 | 2 | tool:move_cursor | put the crosshair at the corner, voxel 0 0 0 | move_cursor |
| S064 | 2 | tool:move_cursor | navigate to position 100, 100, 90 | move_cursor |
| S065 | 2 | tool:move_cursor | set the cursor to the center-ish voxel 60 70 64 | move_cursor |
| S066 | 3 | tool:focus_label | take me to the nodule | focus_label |
| S067 | 3 | tool:focus_label | center the view on label 1 | focus_label |
| S068 | 3 | tool:focus_label | show me where structure 1 is | focus_label |
| S069 | 3 | tool:focus_label | focus on label 1 | focus_label |
| S070 | 3 | tool:focus_label | jump to the middle of label 1 | focus_label |
| S071 | 3 | tool:undo | undo that | undo |
| S072 | 3 | tool:undo | undo my last edit | undo |
| S073 | 3 | tool:undo | take back the last change | undo |
| S074 | 3 | tool:undo | revert the last segmentation edit | undo |
| S075 | 3 | tool:undo | ctrl-z please | undo |
| S076 | 4 | tool:redo | redo it | redo |
| S077 | 4 | tool:redo | redo the last undone edit | redo |
| S078 | 4 | tool:redo | bring that back | redo |
| S079 | 4 | tool:redo | re-apply what I undid | redo |
| S080 | 4 | tool:redo | ctrl-y | redo |
| S081 | 2 | tool:save_workspace | save the workspace to D:/itksnap-build/out_ws.itksnap | save_workspace |
| S082 | 2 | tool:save_workspace | save my session as a workspace file at D:/itksnap-build/sess.itksnap | save_workspace |
| S083 | 2 | tool:save_workspace | export the current project to D:/itksnap-build/proj.itksnap | save_workspace |
| S084 | 2 | tool:save_workspace | preserve everything to a workspace D:/itksnap-build/keep.itksnap | save_workspace |
| S085 | 2 | tool:save_workspace | write the itksnap workspace to D:/itksnap-build/w2.itksnap | save_workspace |
| S086 | 2 | tool:save_statistics | save the statistics to D:/itksnap-build/stats1.txt | save_statistics |
| S087 | 2 | tool:save_statistics | export per-label measurements to D:/itksnap-build/stats2.txt | save_statistics |
| S088 | 2 | tool:save_statistics | write out the volume stats to D:/itksnap-build/stats3.txt | save_statistics |
| S089 | 2 | tool:save_statistics | dump the segmentation statistics into D:/itksnap-build/stats4.txt | save_statistics |
| S090 | 2 | tool:save_statistics | save a stats report at D:/itksnap-build/stats5.txt | save_statistics |
| S091 | 2 | tool:save_labels | save the label definitions to D:/itksnap-build/labels1.txt | save_labels |
| S092 | 2 | tool:save_labels | export label names and colors to D:/itksnap-build/labels2.txt | save_labels |
| S093 | 2 | tool:save_labels | write the label descriptions to D:/itksnap-build/labels3.txt | save_labels |
| S094 | 2 | tool:save_labels | back up the labels to D:/itksnap-build/labels4.txt | save_labels |
| S095 | 2 | tool:save_labels | store the label table at D:/itksnap-build/labels5.txt | save_labels |
| S096 | 2 | tool:save_annotations | save annotations to D:/itksnap-build/ann1.annot | save_annotations |
| S097 | 2 | tool:save_annotations | export the rulers/landmarks to D:/itksnap-build/ann2.annot | save_annotations |
| S098 | 2 | tool:save_annotations | write annotations out to D:/itksnap-build/ann3.annot | save_annotations |
| S099 | 2 | tool:save_annotations | persist annotations at D:/itksnap-build/ann4.annot | save_annotations |
| S100 | 2 | tool:save_annotations | store the annotation set in D:/itksnap-build/ann5.annot | save_annotations |
| S101 | 2 | tool:unload_main_image | close the current image | unload_main_image |
| S102 | 2 | tool:unload_main_image | unload the main image | unload_main_image |
| S103 | 2 | tool:unload_main_image | clear the loaded scan | unload_main_image |
| S104 | 2 | tool:unload_main_image | close the study | unload_main_image |
| S105 | 2 | tool:unload_main_image | remove the image from the viewer | unload_main_image |
| S106 | 1 | tool:get_cursor_info | what's at the crosshair? | get_cursor_info |
| S107 | 1 | tool:get_cursor_info | which label is under the cursor? | get_cursor_info |
| S108 | 1 | tool:get_cursor_info | tell me about the current cursor position | get_cursor_info |
| S109 | 1 | tool:get_cursor_info | what voxel am I on? | get_cursor_info |
| S110 | 1 | tool:get_cursor_info | read out the value/label at the crosshair | get_cursor_info |
| S111 | 3 | tool:load_overlay | load an overlay from D:/x.nii.gz | load_overlay |
| S112 | 3 | tool:load_overlay | add D:/o.nii.gz as an overlay | load_overlay |
| S113 | 3 | tool:load_overlay | overlay the second scan D:/t2.nii.gz | load_overlay |
| S114 | 3 | tool:load_overlay | put D:/pet.nii.gz on top as overlay | load_overlay |
| S115 | 3 | tool:load_overlay | add an overlay image at D:/ov.nii.gz | load_overlay |
| S116 | 3 | tool:load_segmentation | load an existing segmentation from D:/seg.nii.gz | load_segmentation |
| S117 | 3 | tool:load_segmentation | import the mask D:/labels.nii.gz | load_segmentation |
| S118 | 3 | tool:load_segmentation | open a saved segmentation D:/s.nii.gz | load_segmentation |
| S119 | 3 | tool:load_segmentation | bring in the label image D:/m.nii.gz | load_segmentation |
| S120 | 3 | tool:load_segmentation | load segmentation file D:/seg2.nii.gz | load_segmentation |
| S121 | 3 | tool:unload_overlays | remove all overlays | unload_overlays |
| S122 | 3 | tool:unload_overlays | clear the overlay images | unload_overlays |
| S123 | 3 | tool:unload_overlays | take off the overlays | unload_overlays |
| S124 | 3 | tool:unload_overlays | get rid of every overlay | unload_overlays |
| S125 | 3 | tool:unload_overlays | unload all overlay layers | unload_overlays |
| S126 | 3 | tool:load_workspace | open the workspace D:/w.itksnap | load_workspace |
| S127 | 3 | tool:load_workspace | restore session from D:/s.itksnap | load_workspace |
| S128 | 3 | tool:load_workspace | load project D:/p.itksnap | load_workspace |
| S129 | 3 | tool:load_workspace | reopen my saved workspace D:/keep.itksnap | load_workspace |
| S130 | 3 | tool:load_workspace | open itksnap workspace D:/proj.itksnap | load_workspace |
| S131 | 3 | tool:load_labels | load label descriptions from D:/labels.txt | load_labels |
| S132 | 3 | tool:load_labels | import label names from D:/l.txt | load_labels |
| S133 | 3 | tool:load_labels | read the label table from D:/lt.txt | load_labels |
| S134 | 3 | tool:load_labels | restore labels from D:/labs.txt | load_labels |
| S135 | 3 | tool:load_labels | load the label definitions file D:/ld.txt | load_labels |
| S136 | 3 | tool:load_annotations | load annotations from D:/a.annot | load_annotations |
| S137 | 3 | tool:load_annotations | import rulers from D:/r.annot | load_annotations |
| S138 | 3 | tool:load_annotations | open the annotation file D:/an.annot | load_annotations |
| S139 | 3 | tool:load_annotations | restore landmarks from D:/lm.annot | load_annotations |
| S140 | 3 | tool:load_annotations | read annotations out of D:/notes.annot | load_annotations |
| S141 | 3 | tool:smooth_labels | smooth label 1 | smooth_labels |
| S142 | 3 | tool:smooth_labels | clean up the jagged edges of label 1 with 2mm sigma | smooth_labels |
| S143 | 3 | tool:smooth_labels | gaussian-smooth the nodule segmentation | smooth_labels |
| S144 | 3 | tool:smooth_labels | reduce the staircase artifacts on label 1 | smooth_labels |
| S145 | 3 | tool:smooth_labels | smooth structure 1 boundaries | smooth_labels |
| S146 | 3 | tool:interpolate_labels | interpolate the labels across slices | interpolate_labels |
| S147 | 3 | tool:interpolate_labels | fill the gaps between the slices I drew | interpolate_labels |
| S148 | 3 | tool:interpolate_labels | interpolate label 1 between slices | interpolate_labels |
| S149 | 3 | tool:interpolate_labels | connect my sparse slice drawings | interpolate_labels |
| S150 | 3 | tool:interpolate_labels | run slice interpolation on the segmentation | interpolate_labels |
| S151 | 3 | tool:export_slice | export the current axial slice to D:/slice.png | export_slice |
| S152 | 3 | tool:export_slice | save the coronal view to D:/c.png | export_slice |
| S153 | 3 | tool:export_slice | write the sagittal slice to D:/s.png | export_slice |
| S154 | 3 | tool:export_slice | screenshot the axial slice at D:/ax.png | export_slice |
| S155 | 3 | tool:export_slice | export the slice image to D:/out.png | export_slice |
| S156 | 3 | tool:set_layout | switch to axial-only view | set_layout |
| S157 | 3 | tool:set_layout | show me all four panels | set_layout |
| S158 | 3 | tool:set_layout | go to 3D-only layout | set_layout |
| S159 | 3 | tool:set_layout | coronal view please | set_layout |
| S160 | 3 | tool:set_layout | single sagittal pane | set_layout |
| S161 | 3 | tool:update_3d_mesh | rebuild the 3d mesh | update_3d_mesh |
| S162 | 3 | tool:update_3d_mesh | update the 3D surface | update_3d_mesh |
| S163 | 3 | tool:update_3d_mesh | regenerate the 3d model | update_3d_mesh |
| S164 | 3 | tool:update_3d_mesh | refresh the 3D rendering of the segmentation | update_3d_mesh |
| S165 | 3 | tool:update_3d_mesh | make the 3D mesh from the labels | update_3d_mesh |
| S166 | 5 | composite | segment the bright nodule as label 1 and then measure it | threshold_segment, measure_volume |
| S167 | 5 | composite | segment everything above 200 as label 1, then tell me how many voxels it has | threshold_segment, count_voxels |
| S168 | 6 | composite | outline the nodule and center the view on it | threshold_segment, focus_label |
| S169 | 7 | composite | segment the organ (around 100) as label 2 and the nodule (above 200) as label 1, then compare their volumes | threshold_segment, measure_all_labels |
| S170 | 8 | composite | clear the whole segmentation, then re-segment the nodule above 210 into label 1 and measure it | clear_segmentation, threshold_segment, measure_volume |
| S171 | 9 | composite | segment the nodule as label 1, save the workspace to D:/itksnap-build/comp1.itksnap, and save the stats to D:/itksnap-build/comp1_stats.txt | threshold_segment, save_workspace, save_statistics |
| S172 | 8 | composite | segment the nodule into label 1, rename it to lesion, color it red, then report its volume | threshold_segment, rename_label, set_label_color, measure_volume |
| S173 | 7 | composite | segment above 200 as label 1, then undo it so nothing is segmented | threshold_segment, undo |
| S174 | 8 | composite | do a full workup on the bright lesion: segment it, measure it, and take me to it | threshold_segment, measure_volume, focus_label |
| S175 | 7 | composite | segment the organ as label 2, then erase just that label leaving everything else | threshold_segment, clear_label |
| S176 | 9 | composite | segment the nodule as label 1 and the organ as label 2, then merge label 1 into label 2 and measure the result | threshold_segment, threshold_segment, replace_label, measure_volume |
| S177 | 8 | composite | segment everything above 200, move the cursor to voxel 5 5 5, then focus back on the segmentation | threshold_segment, move_cursor, focus_label |
| S178 | 10 | composite | give me a complete report: segment organ and nodule, measure all labels, and export the statistics to D:/itksnap-build/report_all.txt | threshold_segment, threshold_segment, measure_all_labels, save_statistics |
| S179 | 9 | composite | segment the nodule, save the labels to D:/itksnap-build/comp_labels.txt and the workspace to D:/itksnap-build/comp_ws.itksnap | threshold_segment, save_labels, save_workspace |
| S180 | 8 | composite | segment the nodule as label 1, then segment the organ as label 2, then clear the whole thing and confirm it's empty | threshold_segment, threshold_segment, clear_segmentation |
| S181 | 9 | composite | prepare a clean lesion analysis end to end and persist it to D:/itksnap-build/lesion_final.itksnap | threshold_segment, measure_volume, save_workspace |
| S182 | 10 | composite | I need the nodule isolated, quantified in mL, centered in view, and its stats written to D:/itksnap-build/nodule_stats.txt | threshold_segment, measure_volume, focus_label, save_statistics |
| S183 | 10 | composite | segment both structures, rename label 1 to lesion and label 2 to organ, then export all volumes to D:/itksnap-build/named_stats.txt | threshold_segment, threshold_segment, rename_label, rename_label, save_statistics |
| S184 | 9 | composite | reset everything, segment only the exact nodule voxels (value 220) into label 1, verify by measuring it | clear_segmentation, threshold_segment, measure_volume |
| S185 | 4 | adversarial | rotate the 3d model 90 degrees | (none — should refuse) |
| S186 | 5 | adversarial | apply a deep learning kidney segmentation model | (none — should refuse) |
| S187 | 5 | adversarial | register this scan to an atlas | (none — should refuse) |