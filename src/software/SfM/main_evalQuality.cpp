
// Copyright (c) 2012, 2013, 2014, 2015 Pierre MOULON.

// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "openMVG/sfm/sfm.hpp"

#include "software/SfM/io_readGT.hpp"
#include "software/SfM/tools_precisionEvaluationToGt.hpp"
#include "software/SfM/SfMPlyHelper.hpp"

#include "third_party/htmlDoc/htmlDoc.hpp"

#include "third_party/cmdLine/cmdLine.h"

#include <cstdlib>
#include <iostream>

using namespace openMVG;
using namespace openMVG::sfm;

int main(int argc, char **argv)
{
  using namespace std;

  CmdLine cmd;

  std::string
    sGTDirectory,
    sComputedDirectory,
    sOutDir = "";
  int camType = -1; //1: openMVG cam, 2,3: Strechas cam


  cmd.add( make_option('i', sGTDirectory, "gt") );
  cmd.add( make_option('c', sComputedDirectory, "computed") );
  cmd.add( make_option('o', sOutDir, "outdir") );
  cmd.add( make_option('t', camType, "camtype") );

  try {
    if (argc == 1) throw std::string("Invalid command line parameter.");
    cmd.process(argc, argv);
  } catch(const std::string& s) {
    std::cerr << "Usage: " << argv[0] << '\n'
      << "[-i|--gt] path (where ground truth camera trajectory are saved)\n"
      << "[-c|--computed] path (openMVG SfM_Output directory)\n"
      << "[-o|--output] path (where statistics will be saved)\n"
      << "[-t|--camtype] Type of the camera:\n"
      << " -1: autoguess (try 1,2,3),\n"
      << "  1: openMVG (bin),\n"
      << "  2: Strechas 'png.camera' \n"
      << "  3: Strechas 'jpg.camera' ]\n"
      << std::endl;

    std::cerr << s << std::endl;
    return EXIT_FAILURE;
  }

  if (sOutDir.empty())  {
    std::cerr << "\nIt is an invalid output directory" << std::endl;
    return EXIT_FAILURE;
  }

  if (!stlplus::folder_exists(sOutDir))
    stlplus::folder_create(sOutDir);

  //Setup the camera type and the appropriate camera reader
  bool (*fcnReadCamPtr)(const std::string&, Pinhole_Intrinsic&, geometry::Pose3&);
  std::string suffix;

  switch (camType)
  {
    case -1:  // handle auto guess
    {
      if (!stlplus::folder_wildcard(sGTDirectory, "*.bin", false, true).empty())
        camType = 1;
      else if (!stlplus::folder_wildcard(sGTDirectory, "*.png.camera", false, true).empty())
        camType = 2;
      else if (!stlplus::folder_wildcard(sGTDirectory, "*.jpg.camera", false, true).empty())
        camType = 3;
      else if (!stlplus::folder_wildcard(sGTDirectory, "*.PNG.camera", false, true).empty())
        camType = 4;
      else if (!stlplus::folder_wildcard(sGTDirectory, "*.JPG.camera", false, true).empty())
        camType = 5;
      else
        camType = std::numeric_limits<int>::infinity();
    }
    break;
  }
  switch (camType)
  {
    case 1:
      std::cout << "\nusing openMVG Camera";
      fcnReadCamPtr = &read_openMVG_Camera;
      suffix = "bin";
      break;
    case 2:
      std::cout << "\nusing Strechas Camera (png)";
      fcnReadCamPtr = &read_Strecha_Camera;
      suffix = "png.camera";
      break;
    case 3:
      std::cout << "\nusing Strechas Camera (jpg)";
      fcnReadCamPtr = &read_Strecha_Camera;
      suffix = "jpg.camera";
      break;
    case 4:
      std::cout << "\nusing Strechas Camera (PNG)";
      fcnReadCamPtr = &read_Strecha_Camera;
      suffix = "PNG.camera";
      break;
    case 5:
      std::cout << "\nusing Strechas Camera (JPG)";
      fcnReadCamPtr = &read_Strecha_Camera;
      suffix = "JPG.camera";
      break;
    default:
      std::cerr << "Unsupported camera type. Please write your camera reader." << std::endl;
      return EXIT_FAILURE;
  }

  //---------------------------------------
  // Quality evaluation
  //---------------------------------------

  //-- Load GT camera rotations & positions [R|C]:
  SfM_Data sfm_data_gt;
  // READ DATA FROM GT
  std::cout << "\nTry to read data from GT";
  std::vector<std::string> vec_fileNames;
  readGt(fcnReadCamPtr, sGTDirectory, suffix, vec_fileNames, sfm_data_gt.poses, sfm_data_gt.intrinsics);
  std::cout << sfm_data_gt.poses.size() << " gt cameras have been found" << std::endl;

  //-- Load the camera that we have to evaluate
  SfM_Data sfm_data;
  if (!Load(sfm_data, sComputedDirectory, ESfM_Data(VIEWS|INTRINSICS|EXTRINSICS)))
  {
    std::cerr << std::endl
      << "The input SfM_Data file \""<< sComputedDirectory << "\" cannot be read." << std::endl;
    return EXIT_FAILURE;
  }

  // Fill vectors of valid views for evaluation
  std::vector<Vec3> vec_camPosGT, vec_C;
  std::vector<Mat3> vec_camRotGT, vec_camRot;
  for(const auto &iter : sfm_data.GetViews())
  {
    const auto &view = iter.second;
    if(sfm_data.GetPoses().find(view->id_pose) == sfm_data.GetPoses().end())
      continue;

    int id_gt = findIdGT(view->s_Img_path, vec_fileNames);
    if(id_gt == -1)
      continue;

    //-- GT
    const geometry::Pose3 pose_gt = sfm_data_gt.GetPoses().at(id_gt);
    vec_camPosGT.push_back(pose_gt.center());
    vec_camRotGT.push_back(pose_gt.rotation());

    //-- Data to evaluate
    const geometry::Pose3 pose_eval = sfm_data.GetPoses().at(view->id_pose);
    vec_C.push_back(pose_eval.center());
    vec_camRot.push_back(pose_eval.rotation());
  }

  // Visual output of the camera location
  plyHelper::exportToPly(vec_camPosGT, string(stlplus::folder_append_separator(sOutDir) + "camGT.ply").c_str());
  plyHelper::exportToPly(vec_C, string(stlplus::folder_append_separator(sOutDir) + "camComputed.ply").c_str());

  // Evaluation
  htmlDocument::htmlDocumentStream _htmlDocStream("openMVG Quality evaluation.");
  EvaluteToGT(vec_camPosGT, vec_C, vec_camRotGT, vec_camRot, sOutDir, &_htmlDocStream);

  ofstream htmlFileStream( string(stlplus::folder_append_separator(sOutDir) +
    "ExternalCalib_Report.html"));
  htmlFileStream << _htmlDocStream.getDoc();

  return EXIT_SUCCESS;
}

