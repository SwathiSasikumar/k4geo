#include "DD4hep/DetFactoryHelper.h"
#include "DD4hep/Handle.h"
#include "DDRec/MaterialManager.h"
#include "DDRec/Vector3D.h"
#include "XML/Utilities.h"

#include <DDRec/DetectorData.h>

// Taken from
// https://github.com/HEP-FCC/FCCDetectors/blob/main/Detector/DetFCChhECalInclined/src/ECalBarrelInclined_geo.cpp  (go
// there to see older commit history)

// todo: remove gaudi logging and properly capture output
#define endmsg std::endl
#define lLog std::cout
namespace MSG {
const std::string ERROR = " Error: ";
const std::string DEBUG = " Debug: ";
const std::string INFO = " Info: ";
} // namespace MSG

namespace det {
static dd4hep::detail::Ref_t createECalBarrelInclined(dd4hep::Detector& aLcdd, dd4hep::xml::Handle_t aXmlElement,
                                                      dd4hep::SensitiveDetector aSensDet) {

  dd4hep::xml::DetElement xmlDetElem = aXmlElement;
  std::string nameDet = xmlDetElem.nameStr();
  dd4hep::xml::Dimension dim(xmlDetElem.dimensions());
  dd4hep::DetElement caloDetElem(nameDet, xmlDetElem.id());

  // Create air envelope for the whole barrel
  dd4hep::Volume envelopeVol(nameDet + "_vol", dd4hep::Tube(dim.rmin(), dim.rmax(), dim.dz()), aLcdd.material("Air"));
  envelopeVol.setVisAttributes(aLcdd, dim.visStr());

  // Retrieve cryostat data
  dd4hep::xml::DetElement cryostat = aXmlElement.child(_Unicode(cryostat));
  dd4hep::xml::Dimension cryoDim(cryostat.dimensions());
  double cryoThicknessFront = cryoDim.rmin2() - cryoDim.rmin1();
  dd4hep::xml::DetElement cryoFront = cryostat.child(_Unicode(front));
  dd4hep::xml::DetElement cryoBack = cryostat.child(_Unicode(back));
  dd4hep::xml::DetElement cryoSide = cryostat.child(_Unicode(side));
  bool cryoFrontSensitive = cryoFront.isSensitive();
  bool cryoBackSensitive = cryoBack.isSensitive();
  bool cryoSideSensitive = cryoSide.isSensitive();

  // Retrieve active and passive material data
  dd4hep::xml::DetElement calo = aXmlElement.child(_Unicode(calorimeter));
  dd4hep::xml::Dimension caloDim(calo.dimensions());
  dd4hep::xml::DetElement active = calo.child(_Unicode(active));
  std::string activeMaterial = active.materialStr();
  double activeThickness = active.thickness();

  // Retrieve information about active/passive overlap
  dd4hep::xml::DetElement overlap = active.child(_Unicode(overlap));
  double activePassiveOverlap = overlap.offset();
  if (activePassiveOverlap < 0 || activePassiveOverlap > 0.5) {
    // todo: ServiceHandle<IIncidentSvc> incidentSvc("IncidentSvc", "ECalConstruction");
    lLog << MSG::ERROR << "Overlap between active and passive cannot be more than half of passive plane!" << endmsg;
    // todo: incidentSvc->fireIncident(Incident("ECalConstruction", "GeometryFailure"));
  }

  // Retrieve length of layers along electrode, rescaled to active barrel radial extent
  dd4hep::xml::DetElement layers = calo.child(_Unicode(layers));
  uint numLayers = 0;
  std::vector<double> layerHeight;
  double layersTotalHeight = 0;
  for (dd4hep::xml::Collection_t layer_coll(layers, _Unicode(layer)); layer_coll; ++layer_coll) {
    dd4hep::xml::Component layer = layer_coll;
    numLayers += layer.repeat();
    for (int iLay = 0; iLay < layer.repeat(); iLay++) {
      layerHeight.push_back(layer.thickness());
    }
    layersTotalHeight += layer.repeat() * layer.thickness();
  }
  lLog << MSG::DEBUG << "Number of layers: " << numLayers << " total thickness " << layersTotalHeight << endmsg;

  // The following code checks if the xml geometry file contains a constant defining
  // the number of layers the barrel. In that case, it makes the program abort
  // if the number of planes in the xml is different from the one calculated from
  // the geometry. This is because the number of layers is needed
  // in other parts of the code (the readout for the FCC-ee ECAL with
  // inclined modules).
  int nLayers = -1;
  try {
    nLayers = aLcdd.constant<int>("ECalBarrelNumLayers");
  } catch (...) {
    ;
  }
  if (nLayers > 0 && nLayers != int(numLayers)) {
    lLog << MSG::ERROR << "Incorrect number of layers (ECalBarrelNumLayers) in xml file!" << endmsg;
    // todo: incidentSvc->fireIncident(Incident("ECalConstruction", "GeometryFailure"));
    // make the code crash (incidentSvc does not work)
    // Andre, Alvaro, assert replaced by exception
    throw std::runtime_error("Incorrect number of layers (ECalBarrelNumLayers) in xml file!");
  }

  // Retrieve information about the readout
  dd4hep::xml::DetElement readout = calo.child(_Unicode(readout));
  std::string readoutMaterial = readout.materialStr();
  double readoutThickness = readout.thickness();

  // Retrieve info about passive elements
  // (glue, outer - typically steel, inner - typically Pb, and LAr in presampling layer)
  dd4hep::xml::DetElement passive = calo.child(_Unicode(passive));
  dd4hep::xml::DetElement passiveInner = passive.child(_Unicode(inner));
  dd4hep::xml::DetElement passiveInnerMax = passive.child(_Unicode(innerMax));
  dd4hep::xml::DetElement passiveOuter = passive.child(_Unicode(outer));
  dd4hep::xml::DetElement passiveGlue = passive.child(_Unicode(glue));
  std::string passiveInnerMaterial = passiveInner.materialStr();
  std::string passiveOuterMaterial = passiveOuter.materialStr();
  std::string passiveGlueMaterial = passiveGlue.materialStr();
  double passiveInnerThicknessMin = passiveInner.thickness();
  double passiveInnerThicknessMax = passiveInnerMax.thickness();
  double passiveOuterThickness = passiveOuter.thickness();
  double passiveGlueThickness = passiveGlue.thickness();
  double passiveThickness = passiveInnerThicknessMin + passiveOuterThickness + passiveGlueThickness;
  // inclination angle
  double angle = passive.rotation().angle();
  // Retrieve info about bath
  dd4hep::xml::DetElement bath = aXmlElement.child(_Unicode(bath));
  dd4hep::xml::Dimension bathDim(bath.dimensions());
  double bathRmin = bathDim.rmin();
  double bathRmax = bathDim.rmax();
  dd4hep::Tube bathOuterShape(bathRmin, bathRmax, caloDim.dz()); // make it 4 volumes + 5th for detector envelope
  dd4hep::Tube bathAndServicesOuterShape(cryoDim.rmin2(), cryoDim.rmax1(),
                                         caloDim.dz()); // make it 4 volumes + 5th for detector envelope

  // 1. Create cryostat
  if (cryoThicknessFront > 0) {
    dd4hep::Tube cryoFrontShape(cryoDim.rmin1(), cryoDim.rmin2(), cryoDim.dz());
    dd4hep::Tube cryoBackShape(cryoDim.rmax1(), cryoDim.rmax2(), cryoDim.dz());
    dd4hep::Tube cryoSideOuterShape(cryoDim.rmin2(), cryoDim.rmax1(), cryoDim.dz());
    dd4hep::SubtractionSolid cryoSideShape(cryoSideOuterShape, bathAndServicesOuterShape);
    lLog << MSG::INFO << "ECAL cryostat: front: rmin (cm) = " << cryoDim.rmin1() << " rmax (cm) = " << cryoDim.rmin2()
         << " dz (cm) = " << cryoDim.dz() << endmsg;
    lLog << MSG::INFO << "ECAL cryostat: back: rmin (cm) = " << cryoDim.rmax1() << " rmax (cm) = " << cryoDim.rmax2()
         << " dz (cm) = " << cryoDim.dz() << endmsg;
    lLog << MSG::INFO << "ECAL cryostat: side: rmin (cm) = " << cryoDim.rmin2() << " rmax (cm) = " << cryoDim.rmax1()
         << " dz (cm) = " << cryoDim.dz() - caloDim.dz() << endmsg;
    dd4hep::Volume cryoFrontVol(cryostat.nameStr() + "_front", cryoFrontShape, aLcdd.material(cryostat.materialStr()));
    dd4hep::Volume cryoBackVol(cryostat.nameStr() + "_back", cryoBackShape, aLcdd.material(cryostat.materialStr()));
    dd4hep::Volume cryoSideVol(cryostat.nameStr() + "_side", cryoSideShape, aLcdd.material(cryostat.materialStr()));
    dd4hep::PlacedVolume cryoFrontPhysVol = envelopeVol.placeVolume(cryoFrontVol);
    dd4hep::PlacedVolume cryoBackPhysVol = envelopeVol.placeVolume(cryoBackVol);
    dd4hep::PlacedVolume cryoSidePhysVol = envelopeVol.placeVolume(cryoSideVol);
    if (cryoFrontSensitive) {
      cryoFrontVol.setSensitiveDetector(aSensDet);
      cryoFrontPhysVol.addPhysVolID("cryo", 1);
      cryoFrontPhysVol.addPhysVolID("type", 1);
      lLog << MSG::INFO << "Cryostat front volume set as sensitive" << endmsg;
    }
    if (cryoBackSensitive) {
      cryoBackVol.setSensitiveDetector(aSensDet);
      cryoBackPhysVol.addPhysVolID("cryo", 1);
      cryoBackPhysVol.addPhysVolID("type", 2);
      lLog << MSG::INFO << "Cryostat back volume set as sensitive" << endmsg;
    }
    if (cryoSideSensitive) {
      cryoSideVol.setSensitiveDetector(aSensDet);
      cryoSidePhysVol.addPhysVolID("cryo", 1);
      cryoSidePhysVol.addPhysVolID("type", 3);
      lLog << MSG::INFO << "Cryostat front volume set as sensitive" << endmsg;
    }
    dd4hep::DetElement cryoFrontDetElem(caloDetElem, "cryo_front", 0);
    cryoFrontDetElem.setPlacement(cryoFrontPhysVol);
    dd4hep::DetElement cryoBackDetElem(caloDetElem, "cryo_back", 0);
    cryoBackDetElem.setPlacement(cryoBackPhysVol);
    dd4hep::DetElement cryoSideDetElem(caloDetElem, "cryo_side", 0);
    cryoSideDetElem.setPlacement(cryoSidePhysVol);
    // 1.2. Create place-holder for services
    dd4hep::Tube servicesFrontShape(cryoDim.rmin2(), bathRmin, caloDim.dz());
    dd4hep::Tube servicesBackShape(bathRmax, cryoDim.rmax1(), caloDim.dz());
    lLog << MSG::INFO << "ECAL services: front: rmin (cm) = " << cryoDim.rmin2() << " rmax (cm) = " << bathRmin
         << " dz (cm) = " << caloDim.dz() << endmsg;
    lLog << MSG::INFO << "ECAL services: back: rmin (cm) = " << bathRmax << " rmax (cm) = " << cryoDim.rmax1()
         << " dz (cm) = " << caloDim.dz() << endmsg;
    dd4hep::Volume servicesFrontVol("services_front", servicesFrontShape, aLcdd.material(activeMaterial));
    dd4hep::Volume servicesBackVol("services_back", servicesBackShape, aLcdd.material(activeMaterial));
    dd4hep::PlacedVolume servicesFrontPhysVol = envelopeVol.placeVolume(servicesFrontVol);
    dd4hep::PlacedVolume servicesBackPhysVol = envelopeVol.placeVolume(servicesBackVol);
    if (cryoFrontSensitive) {
      servicesFrontVol.setSensitiveDetector(aSensDet);
      servicesFrontPhysVol.addPhysVolID("cryo", 1);
      servicesFrontPhysVol.addPhysVolID("type", 4);
      lLog << MSG::INFO << "Services front volume set as sensitive" << endmsg;
    }
    if (cryoBackSensitive) {
      servicesBackVol.setSensitiveDetector(aSensDet);
      servicesBackPhysVol.addPhysVolID("cryo", 1);
      servicesBackPhysVol.addPhysVolID("type", 5);
      lLog << MSG::INFO << "Services back volume set as sensitive" << endmsg;
    }
    dd4hep::DetElement servicesFrontDetElem(caloDetElem, "services_front", 0);
    servicesFrontDetElem.setPlacement(servicesFrontPhysVol);
    dd4hep::DetElement servicesBackDetElem(caloDetElem, "services_back", 0);
    servicesBackDetElem.setPlacement(servicesBackPhysVol);
  }

  // 2. Create bath that is inside the cryostat and surrounds the detector
  //    Bath is filled with active material -> but not sensitive
  dd4hep::Volume bathVol(activeMaterial + "_bath", bathOuterShape, aLcdd.material(activeMaterial));
  lLog << MSG::INFO << "ECAL bath: material = " << activeMaterial << " rmin (cm) =  " << bathRmin
       << " rmax (cm) = " << bathRmax << " thickness in front of ECal (cm) = " << caloDim.rmin() - cryoDim.rmin2()
       << " thickness behind ECal (cm) = " << cryoDim.rmax1() - caloDim.rmax() << endmsg;

  // 3. Create the calorimeter by placing the passive material, trapezoid active layers, readout and again trapezoid
  // active layers in the bath.
  // sensitive detector for the layers
  dd4hep::SensitiveDetector sd = aSensDet;
  dd4hep::xml::Dimension sdType = xmlDetElem.child(_U(sensitive));
  sd.setType(sdType.typeStr());
  lLog << MSG::INFO << "ECAL calorimeter volume rmin (cm) =  " << caloDim.rmin() << " rmax (cm) = " << caloDim.rmax()
       << endmsg;

  // 3.a. Create the passive planes, readout in between of 2 passive planes and the remaining space fill with active
  // material

  //////////////////////////////
  // PASSIVE PLANES
  //////////////////////////////

  // passive volumes consist of inner part and two outer, joined by glue
  lLog << MSG::INFO << "passive inner material = " << passiveInnerMaterial << "\n"
       << " and outer material = " << passiveOuterMaterial << "\n"
       << " thickness of inner part at inner radius (cm) =  " << passiveInnerThicknessMin << "\n"
       << " thickness of inner part at outer radius (cm) =  " << passiveInnerThicknessMax << "\n"
       << " thickness of outer part (cm) =  " << passiveOuterThickness << "\n"
       << " thickness of total (cm) =  " << passiveThickness << "\n"
       << " rotation angle = " << angle << endmsg;

  // calculate number of modules
  uint numPlanes =
      round(M_PI / asin((passiveThickness + activeThickness + readoutThickness) / (2. * caloDim.rmin() * cos(angle))));

  double dPhi = 2. * M_PI / numPlanes;
  lLog << MSG::INFO << "number of passive plates = " << numPlanes << " azim. angle difference =  " << dPhi << endmsg;
  lLog << MSG::INFO << " distance at inner radius (cm) = " << 2. * M_PI * caloDim.rmin() / numPlanes << "\n"
       << " distance at outer radius (cm) = " << 2. * M_PI * caloDim.rmax() / numPlanes << endmsg;

  // The following code checks if the xml geometry file contains a constant defining
  // the number of planes in the barrel. In that case, it makes the program abort
  // if the number of planes in the xml is different from the one calculated from
  // the geometry. This is because the number of plane information (retrieved from the
  // xml) is used in other parts of the code (the readout for the FCC-ee ECAL with
  // inclined modules). In principle the code above should be refactored so that the number
  // of planes is one of the inputs of the calculation and other geometrical parameters
  // are adjusted accordingly. This is left for the future, and we use the workaround
  // below to enforce for the time being that the number of planes is "correct"
  int nModules = -1;
  try {
    nModules = aLcdd.constant<int>("ECalBarrelNumPlanes");
  } catch (...) {
    ;
  }
  if (nModules > 0 && nModules != int(numPlanes)) {
    lLog << MSG::ERROR << "Incorrect number of planes (ECalBarrelNumPlanes) in xml file!" << endmsg;
    // todo: incidentSvc->fireIncident(Incident("ECalConstruction", "GeometryFailure"));
    // make the code crash (incidentSvc does not work)
    // Andre, Alvaro, assert replaced by exception
    throw std::runtime_error("Incorrect number of planes (ECalBarrelNumPlanes) in xml file!");
  }
  // Readout is in the middle between two passive planes
  double offsetPassivePhi = caloDim.offset() + dPhi / 2.;
  double offsetReadoutPhi = caloDim.offset() + 0;
  lLog << MSG::INFO << "readout material = " << readoutMaterial << "\n"
       << " thickness of readout planes (cm) =  " << readoutThickness << "\n number of readout layers = " << numLayers
       << endmsg;

  // min and max radius of active calorimeter volume, and its radial thickness
  double Rmin = caloDim.rmin();
  double Rmax = caloDim.rmax();
  double dR = Rmax - Rmin;
  // electrode length, given inclination angle and min/max radius of the active calorimeter volume
  double planeLength = -Rmin * cos(angle) + sqrt(pow(Rmax, 2) - pow(Rmin * sin(angle), 2));
  lLog << MSG::INFO << "thickness of calorimeter (cm) = " << dR << "\n"
       << " length of passive or readout planes (cm) =  " << planeLength << endmsg;

  // calculate the thickness of the passive material in each layer
  // it's not constant in case of trapezoidal absorbers  (passiveInnerThicknessMax != passiveInnerThicknessMin)
  // the code calculates the (max) passive thickness per layer i.e. at Rout of layer
  // rescaling by runningHeight / (Rmax - Rmin)
  // GM : should be OK since both Rmax-Rmin and runningHeight are radial extents
  // but shouldn't it be simpler to define layerHeight as the length along the electrode
  // and here use as rescaling factor runningHeight / layersTotalHeight ?
  // This is only place where the fact that the layer thicknesses in the xml are rescaled to
  // the radial extent of the calo is used, because after this block of code the
  // layer thicknesses are rescaled to the electrode length.
  // So maybe we could implement in the xml the length along the electrode, and here
  // rescale by runningHeight/Ltot where runningHeight is calculated along the electrode direction?
  std::vector<double> passiveInnerThicknessLayer(numLayers + 1);
  double runningHeight = 0;
  for (uint iLay = 0; iLay < numLayers; iLay++) {
    passiveInnerThicknessLayer[iLay] =
        passiveInnerThicknessMin +
        (passiveInnerThicknessMax - passiveInnerThicknessMin) * (runningHeight) / (Rmax - Rmin);
    runningHeight += layerHeight[iLay];
  }
  passiveInnerThicknessLayer[numLayers] =
      passiveInnerThicknessMin +
      (passiveInnerThicknessMax - passiveInnerThicknessMin) * (runningHeight) / (Rmax - Rmin);

  // calculate the angle of the passive elements if trapezoidal absorbers are used (Max != Min)
  // if parallel absorbers are used then passiveAngle = 0 and cosPassiveAngle = 1
  double passiveAngle = atan2((passiveInnerThicknessMax - passiveInnerThicknessMin) / 2., planeLength);
  double cosPassiveAngle = cos(passiveAngle);
  double rotatedOuterThickness = passiveOuterThickness / cosPassiveAngle;
  double rotatedGlueThickness = passiveGlueThickness / cosPassiveAngle;

  // rescale layer thicknesses from xml so that they now represent the real
  // lengths along the electrode direction
  double scaleLayerThickness = planeLength / layersTotalHeight;
  layersTotalHeight = 0;
  for (uint iLay = 0; iLay < numLayers; iLay++) {
    layerHeight[iLay] *= scaleLayerThickness;

    layersTotalHeight += layerHeight[iLay];
    lLog << MSG::DEBUG << "Thickness of layer " << iLay << " : " << layerHeight[iLay] << endmsg;
  }

  // distance from the center of the electrode to the center of the 1st layer, in the electrode direction
  double layerFirstOffset = -planeLength / 2. + layerHeight[0] / 2.;

  // in the first calo layer we use a different material (LAr instead of Pb) for the inner passive material
  // to sample more uniformly for the upstream correction. So we need to split the volume into
  // passiveInnerShapeFirstLayer (for first layer) and passiveInnerShape (for other layers)
  // passiveInner elements' lengths along electrode are length of layer0 and suf of lengths of other layers
  // for the other passive elements, which are boxes, their length has to be corrected for the rotation
  // needed in case of trapezoidal absorbers, leading to the 1 / cosPassiveAngle factor
  // For the same reason, their thickness in the direction perpendicular to the electrode
  // has to be scaled by 1 / cosPassiveAngle, and thus we use rotatedOuterThickness and rotatedGlueThickness
  // here
  dd4hep::Trd1 passiveShape(passiveInnerThicknessMin / 2. + rotatedOuterThickness / 2. + rotatedGlueThickness / 2.,
                            passiveInnerThicknessMax / 2. + rotatedOuterThickness / 2. + rotatedGlueThickness / 2.,
                            caloDim.dz(), planeLength / 2.);
  dd4hep::Trd1 passiveInnerShapeFirstLayer(passiveInnerThicknessMin / 2., passiveInnerThicknessLayer[1] / 2.,
                                           caloDim.dz(), layerHeight[0] / 2.);
  dd4hep::Trd1 passiveInnerShape(passiveInnerThicknessLayer[1] / 2., passiveInnerThicknessMax / 2., caloDim.dz(),
                                 planeLength / 2. - layerHeight[0] / 2.);
  dd4hep::Box passiveOuterShape(passiveOuterThickness / 4., caloDim.dz(), planeLength / 2. / cosPassiveAngle);
  dd4hep::Box passiveGlueShape(passiveGlueThickness / 4., caloDim.dz(), planeLength / 2. / cosPassiveAngle);
  dd4hep::Volume passiveVol("passive", passiveShape, aLcdd.material("Air"));
  dd4hep::Volume passiveInnerVol(passiveInnerMaterial + "_passive", passiveInnerShape,
                                 aLcdd.material(passiveInnerMaterial));
  dd4hep::Volume passiveInnerVolFirstLayer(activeMaterial + "_passive", passiveInnerShapeFirstLayer,
                                           aLcdd.material(activeMaterial));
  dd4hep::Volume passiveOuterVol(passiveOuterMaterial + "_passive", passiveOuterShape,
                                 aLcdd.material(passiveOuterMaterial));
  dd4hep::Volume passiveGlueVol(passiveGlueMaterial + "_passive", passiveGlueShape,
                                aLcdd.material(passiveGlueMaterial));

  if (passiveInner.isSensitive()) {
    lLog << MSG::DEBUG << "Passive inner volume set as sensitive" << endmsg;
    // inner part starts at second layer
    double layerOffset = layerFirstOffset + layerHeight[1] / 2.;
    for (uint iLayer = 1; iLayer < numLayers; iLayer++) {
      // dd4hep::Box layerPassiveInnerShape(passiveInnerThickness / 2., caloDim.dz(), layerHeight[iLayer] / 2.);
      dd4hep::Trd1 layerPassiveInnerShape(passiveInnerThicknessLayer[iLayer] / 2.,
                                          passiveInnerThicknessLayer[iLayer + 1] / 2., caloDim.dz(),
                                          layerHeight[iLayer] / 2.);
      dd4hep::Volume layerPassiveInnerVol(passiveInnerMaterial, layerPassiveInnerShape,
                                          aLcdd.material(passiveInnerMaterial));
      layerPassiveInnerVol.setSensitiveDetector(aSensDet);
      dd4hep::PlacedVolume layerPassiveInnerPhysVol =
          passiveInnerVol.placeVolume(layerPassiveInnerVol, dd4hep::Position(0, 0, layerOffset));
      layerPassiveInnerPhysVol.addPhysVolID("layer", iLayer);
      dd4hep::DetElement layerPassiveInnerDetElem("layer", iLayer);
      layerPassiveInnerDetElem.setPlacement(layerPassiveInnerPhysVol);
      if (iLayer != numLayers - 1) {
        layerOffset += layerHeight[iLayer] / 2. + layerHeight[iLayer + 1] / 2.;
      }
    }
  }
  if (passiveOuter.isSensitive()) {
    lLog << MSG::DEBUG << "Passive outer volume set as sensitive" << endmsg;
    double layerOffset = layerFirstOffset / cosPassiveAngle;
    for (uint iLayer = 0; iLayer < numLayers; iLayer++) {
      dd4hep::Box layerPassiveOuterShape(passiveOuterThickness / 4., caloDim.dz(),
                                         layerHeight[iLayer] / 2. / cosPassiveAngle);
      dd4hep::Volume layerPassiveOuterVol(passiveOuterMaterial, layerPassiveOuterShape,
                                          aLcdd.material(passiveOuterMaterial));
      layerPassiveOuterVol.setSensitiveDetector(aSensDet);
      dd4hep::PlacedVolume layerPassiveOuterPhysVol =
          passiveOuterVol.placeVolume(layerPassiveOuterVol, dd4hep::Position(0, 0, layerOffset));
      layerPassiveOuterPhysVol.addPhysVolID("layer", iLayer);
      dd4hep::DetElement layerPassiveOuterDetElem("layer", iLayer);
      layerPassiveOuterDetElem.setPlacement(layerPassiveOuterPhysVol);
      if (iLayer != numLayers - 1) {
        layerOffset += (layerHeight[iLayer] / 2. + layerHeight[iLayer + 1] / 2.) / cosPassiveAngle;
      }
    }
  }
  if (passiveGlue.isSensitive()) {
    lLog << MSG::DEBUG << "Passive glue volume set as sensitive" << endmsg;
    double layerOffset = layerFirstOffset / cosPassiveAngle;
    for (uint iLayer = 0; iLayer < numLayers; iLayer++) {
      dd4hep::Box layerPassiveGlueShape(passiveGlueThickness / 4., caloDim.dz(),
                                        layerHeight[iLayer] / 2. / cosPassiveAngle);
      dd4hep::Volume layerPassiveGlueVol(passiveGlueMaterial, layerPassiveGlueShape,
                                         aLcdd.material(passiveGlueMaterial));
      layerPassiveGlueVol.setSensitiveDetector(aSensDet);
      dd4hep::PlacedVolume layerPassiveGluePhysVol =
          passiveGlueVol.placeVolume(layerPassiveGlueVol, dd4hep::Position(0, 0, layerOffset));
      layerPassiveGluePhysVol.addPhysVolID("layer", iLayer);
      dd4hep::DetElement layerPassiveGlueDetElem("layer", iLayer);
      layerPassiveGlueDetElem.setPlacement(layerPassiveGluePhysVol);
      if (iLayer != numLayers - 1) {
        layerOffset += (layerHeight[iLayer] / 2. + layerHeight[iLayer + 1] / 2.) / cosPassiveAngle;
      }
    }
  }

  // translate and rotate the elements of the module appropriately
  // the Pb absorber does not need to be rotated, but the glue and
  // the outer absorbers have to, in case of trapezoidal absorbers.
  // The glue and steel absorbers also have to be translated in x-y
  // to the proper positions on the two sides of the inner absorber
  dd4hep::PlacedVolume passiveInnerPhysVol =
      passiveVol.placeVolume(passiveInnerVol, dd4hep::Position(0, 0, layerHeight[0] / 2.));
  dd4hep::PlacedVolume passiveInnerPhysVolFirstLayer =
      passiveVol.placeVolume(passiveInnerVolFirstLayer, dd4hep::Position(0, 0, layerFirstOffset));
  dd4hep::PlacedVolume passiveOuterPhysVolBelow = passiveVol.placeVolume(
      passiveOuterVol,
      dd4hep::Transform3D(dd4hep::RotationY(-passiveAngle),
                          dd4hep::Position(-(passiveInnerThicknessMin + passiveInnerThicknessMax) / 4. -
                                               rotatedGlueThickness / 2. - rotatedOuterThickness / 4.,
                                           0, 0)));
  dd4hep::PlacedVolume passiveOuterPhysVolAbove = passiveVol.placeVolume(
      passiveOuterVol, dd4hep::Transform3D(dd4hep::RotationY(passiveAngle),
                                           dd4hep::Position((passiveInnerThicknessMin + passiveInnerThicknessMax) / 4. +
                                                                rotatedGlueThickness / 2. + rotatedOuterThickness / 4.,
                                                            0, 0)));
  dd4hep::PlacedVolume passiveGluePhysVolBelow = passiveVol.placeVolume(
      passiveGlueVol, dd4hep::Transform3D(dd4hep::RotationY(-passiveAngle),
                                          dd4hep::Position(-(passiveInnerThicknessMin + passiveInnerThicknessMax) / 4. -
                                                               rotatedGlueThickness / 4.,
                                                           0, 0)));
  dd4hep::PlacedVolume passiveGluePhysVolAbove = passiveVol.placeVolume(
      passiveGlueVol, dd4hep::Transform3D(dd4hep::RotationY(passiveAngle),
                                          dd4hep::Position((passiveInnerThicknessMin + passiveInnerThicknessMax) / 4. +
                                                               rotatedGlueThickness / 4.,
                                                           0, 0)));
  passiveInnerPhysVol.addPhysVolID("subtype", 0);
  passiveInnerPhysVolFirstLayer.addPhysVolID("subtype", 0);
  passiveOuterPhysVolBelow.addPhysVolID("subtype", 1);
  passiveOuterPhysVolAbove.addPhysVolID("subtype", 2);
  passiveGluePhysVolBelow.addPhysVolID("subtype", 3);
  passiveGluePhysVolAbove.addPhysVolID("subtype", 4);
  if (passiveInner.isSensitive()) {
    passiveInnerVolFirstLayer.setSensitiveDetector(aSensDet);
    passiveInnerPhysVolFirstLayer.addPhysVolID("layer", 0);
    dd4hep::DetElement passiveInnerDetElemFirstLayer("layer", 0);
    passiveInnerDetElemFirstLayer.setPlacement(passiveInnerPhysVolFirstLayer);
  }

  //////////////////////////////
  // READOUT PLANES
  //////////////////////////////
  dd4hep::Box readoutShape(readoutThickness / 2., caloDim.dz(), planeLength / 2.);
  dd4hep::Volume readoutVol(readoutMaterial, readoutShape, aLcdd.material(readoutMaterial));
  if (readout.isSensitive()) {
    lLog << MSG::INFO << "Readout volume set as sensitive" << endmsg;
    double layerOffset = layerFirstOffset;
    for (uint iLayer = 0; iLayer < numLayers; iLayer++) {
      dd4hep::Box layerReadoutShape(readoutThickness / 2., caloDim.dz(), layerHeight[iLayer] / 2.);
      dd4hep::Volume layerReadoutVol(readoutMaterial, layerReadoutShape, aLcdd.material(readoutMaterial));
      layerReadoutVol.setSensitiveDetector(aSensDet);
      dd4hep::PlacedVolume layerReadoutPhysVol =
          readoutVol.placeVolume(layerReadoutVol, dd4hep::Position(0, 0, layerOffset));
      layerReadoutPhysVol.addPhysVolID("layer", iLayer);
      dd4hep::DetElement layerReadoutDetElem("layer", iLayer);
      layerReadoutDetElem.setPlacement(layerReadoutPhysVol);
      if (iLayer != numLayers - 1) {
        layerOffset += layerHeight[iLayer] / 2. + layerHeight[iLayer + 1] / 2.;
      }
    }
  }

  //////////////////////////////
  // ACTIVE
  //////////////////////////////

  // The active (LAr/LKr) elements are constructed subtracting from their
  // envelope the readout and passive elements
  // This requires some calculations to

  // Thickness of active layers at inner radius and outer ( = distance between passive plane and readout plane)
  // at inner radius: distance projected at plane perpendicular to readout plane
  double activeInThickness = Rmin * sin(dPhi / 2.) * cos(angle);
  activeInThickness -= passiveThickness * (0.5 - activePassiveOverlap);
  // at outer radius: distance projected at plane perpendicular to readout plane
  double activeOutThickness = (Rmin + planeLength) * sin(dPhi / 2.) * cos(angle);
  // make correction for outer readius caused by inclination angle
  // first calculate intersection of readout plane and plane parallel to shifted passive plane
  double xIntersect = (Rmin * (tan(angle) - cos(dPhi / 2.) * tan(angle + dPhi / 2.)) - planeLength * sin(dPhi / 2.)) /
                      (tan(angle) - tan(angle + dPhi / 2.));
  double yIntersect = tan(angle) * xIntersect + Rmin * (sin(dPhi / 2.) - tan(angle)) + planeLength * sin(dPhi / 2.);
  // distance from inner radius to intersection
  double correction =
      planeLength - sqrt(pow(xIntersect - Rmin * cos(dPhi / 2), 2) + pow(yIntersect - Rmin * sin(dPhi / 2), 2));
  // correction to the active thickness
  activeOutThickness += 2. * correction * sin(dPhi / 4.);
  activeOutThickness -= passiveThickness * (0.5 - activePassiveOverlap);
  // print the active layer dimensions
  double activeInThicknessAfterSubtraction =
      2. * activeInThickness - readoutThickness - 2. * activePassiveOverlap * passiveThickness;
  double activeOutThicknessAfterSubtraction =
      2. * activeOutThickness - readoutThickness -
      2. * activePassiveOverlap *
          (passiveThickness + passiveInnerThicknessMax - passiveInnerThicknessMin); // correct thickness for trapezoid
  lLog << MSG::INFO << "active material = " << activeMaterial
       << " active layers thickness at inner radius (cm) = " << activeInThicknessAfterSubtraction
       << " thickness at outer radious (cm) = " << activeOutThicknessAfterSubtraction << " making "
       << (activeOutThicknessAfterSubtraction - activeInThicknessAfterSubtraction) * 100 /
              activeInThicknessAfterSubtraction
       << " % increase." << endmsg;
  lLog << MSG::INFO
       << "active passive initial overlap (before subtraction) (cm) = " << passiveThickness * activePassiveOverlap
       << " = " << activePassiveOverlap * 100 << " %" << endmsg;

  // creating shape for rows of layers (active material between two passive planes, with readout in the middle)
  // first define area between two passive planes, area can reach up to the symmetry axis of passive plane
  dd4hep::Trd1 activeOuterShape(activeInThickness, activeOutThickness, caloDim.dz(), planeLength / 2.);
  // subtract readout shape from the middle
  dd4hep::SubtractionSolid activeShapeNoReadout(activeOuterShape, readoutShape);

  // make calculation for active plane that is inclined with 0 deg (= offset + angle)
  double Cx = Rmin * cos(-angle) + planeLength / 2.;
  double Cy = Rmin * sin(-angle);
  double Ax = Rmin * cos(-angle + dPhi / 2.) + planeLength / 2. * cos(dPhi / 2.);
  double Ay = Rmin * sin(-angle + dPhi / 2.) + planeLength / 2. * sin(dPhi / 2.);
  double CAx = fabs(Ax - Cx);
  double CAy = fabs(Ay - Cy);
  double zprim, xprim;
  zprim = CAx;
  xprim = CAy;

  double Bx = Rmin * cos(-angle - dPhi / 2.) + planeLength / 2. * cos(-dPhi / 2.);
  double By = Rmin * sin(-angle - dPhi / 2.) + planeLength / 2. * sin(-dPhi / 2.);
  double CBx = fabs(Bx - Cx);
  double CBy = fabs(By - Cy);
  double zprimB, xprimB;
  zprimB = CBx;
  xprimB = CBy;

  // subtract passive volume above
  dd4hep::SubtractionSolid activeShapeNoPassiveAbove(
      activeShapeNoReadout, passiveShape,
      dd4hep::Transform3D(dd4hep::RotationY(-dPhi / 2.), dd4hep::Position(-fabs(xprim), 0, fabs(zprim))));
  // subtract passive volume below
  dd4hep::SubtractionSolid activeShape(
      activeShapeNoPassiveAbove, passiveShape,
      dd4hep::Transform3D(dd4hep::RotationY(dPhi / 2.), dd4hep::Position(fabs(xprimB), 0, -fabs(zprimB))));
  dd4hep::Volume activeVol("active", activeShape, aLcdd.material("Air"));

  std::vector<dd4hep::PlacedVolume> layerPhysVols;
  // place layers within active volume
  std::vector<double> layerInThickness;
  std::vector<double> layerOutThickness;
  double layerIncreasePerUnitThickness = (activeOutThickness - activeInThickness) / layersTotalHeight;
  for (uint iLay = 0; iLay < numLayers; iLay++) {
    if (iLay == 0) {
      layerInThickness.push_back(activeInThickness);
    } else {
      layerInThickness.push_back(layerOutThickness[iLay - 1]);
    }
    layerOutThickness.push_back(layerInThickness[iLay] + layerIncreasePerUnitThickness * layerHeight[iLay]);
  }
  double layerOffset = layerFirstOffset;
  for (uint iLayer = 0; iLayer < numLayers; iLayer++) {
    dd4hep::Trd1 layerOuterShape(layerInThickness[iLayer], layerOutThickness[iLayer], caloDim.dz(),
                                 layerHeight[iLayer] / 2.);
    dd4hep::SubtractionSolid layerShapeNoReadout(layerOuterShape, readoutShape);
    dd4hep::SubtractionSolid layerShapeNoPassiveAbove(
        layerShapeNoReadout, passiveShape,
        dd4hep::Transform3D(dd4hep::RotationY(-dPhi / 2.),
                            dd4hep::Position(-fabs(xprim), 0, fabs(zprim) - layerOffset)));
    // subtract passive volume below
    dd4hep::SubtractionSolid layerShape(
        layerShapeNoPassiveAbove, passiveShape,
        dd4hep::Transform3D(dd4hep::RotationY(dPhi / 2.),
                            dd4hep::Position(fabs(xprimB), 0, -fabs(zprimB) - layerOffset)));
    dd4hep::Volume layerVol("layer", layerShape, aLcdd.material(activeMaterial));
    layerVol.setSensitiveDetector(aSensDet);
    layerPhysVols.push_back(activeVol.placeVolume(layerVol, dd4hep::Position(0, 0, layerOffset)));
    layerPhysVols.back().addPhysVolID("layer", iLayer);
    if (iLayer != numLayers - 1) {
      layerOffset += layerHeight[iLayer] / 2. + layerHeight[iLayer + 1] / 2.;
    }
  }

  dd4hep::DetElement bathDetElem(caloDetElem, "bath", 1);
  std::vector<dd4hep::PlacedVolume> activePhysVols;
  // Next place elements: passive planes, readout planes and rows of layers
  for (uint iPlane = 0; iPlane < numPlanes; iPlane++) {
    // first calculate positions of passive and readout planes
    // PASSIVE
    // calculate centre position of the plane without plane rotation
    double phi = offsetPassivePhi + iPlane * dPhi;
    double xRadial = (Rmin + planeLength / 2.) * cos(phi);
    double yRadial = (Rmin + planeLength / 2.) * sin(phi);
    // calculate position of the beginning of plane
    double xRmin = Rmin * cos(phi);
    double yRmin = Rmin * sin(phi);
    // rotate centre by angle wrt beginning of plane
    double xRotated = xRmin + (xRadial - xRmin) * cos(angle) - (yRadial - yRmin) * sin(angle);
    double yRotated = yRmin + (xRadial - xRmin) * sin(angle) + (yRadial - yRmin) * cos(angle);
    dd4hep::Transform3D transform(dd4hep::RotationX(-M_PI / 2.)     // to get in XY plane
                                      * dd4hep::RotationY(M_PI / 2. // to get pointed towards centre
                                                          - phi - angle),
                                  dd4hep::Position(xRotated, yRotated, 0));
    dd4hep::PlacedVolume passivePhysVol = bathVol.placeVolume(passiveVol, transform);
    passivePhysVol.addPhysVolID("module", iPlane);
    passivePhysVol.addPhysVolID("type", 1); // 0 = active, 1 = passive, 2 = readout
    dd4hep::DetElement passiveDetElem(bathDetElem, "passive" + std::to_string(iPlane), iPlane);
    passiveDetElem.setPlacement(passivePhysVol);

    // READOUT
    // calculate centre position of the plane without plane rotation
    double phiRead = offsetReadoutPhi + iPlane * dPhi;
    double xRadialRead = (Rmin + planeLength / 2.) * cos(phiRead);
    double yRadialRead = (Rmin + planeLength / 2.) * sin(phiRead);
    // calculate position of the beginning of plane
    double xRminRead = Rmin * cos(phiRead);
    double yRminRead = Rmin * sin(phiRead);
    // rotate centre by angle wrt beginning of plane
    double xRotatedRead = xRminRead + (xRadialRead - xRminRead) * cos(angle) - (yRadialRead - yRminRead) * sin(angle);
    double yRotatedRead = yRminRead + (xRadialRead - xRminRead) * sin(angle) + (yRadialRead - yRminRead) * cos(angle);
    dd4hep::Transform3D transformRead(dd4hep::RotationX(-M_PI / 2.)     // to get in XY plane
                                          * dd4hep::RotationY(M_PI / 2. // to get pointed towards centre
                                                              - phiRead - angle),
                                      dd4hep::Position(xRotatedRead, yRotatedRead, 0));
    dd4hep::PlacedVolume readoutPhysVol = bathVol.placeVolume(readoutVol, transformRead);
    readoutPhysVol.addPhysVolID("module", iPlane);
    readoutPhysVol.addPhysVolID("type", 2); // 0 = active, 1 = passive, 2 = readout
    dd4hep::DetElement readoutDetElem(bathDetElem, "readout" + std::to_string(iPlane), iPlane);
    readoutDetElem.setPlacement(readoutPhysVol);

    // ACTIVE
    dd4hep::Rotation3D rotationActive(dd4hep::RotationX(-M_PI / 2) * dd4hep::RotationY(M_PI / 2 - phiRead - angle));
    activePhysVols.push_back(bathVol.placeVolume(
        activeVol, dd4hep::Transform3D(rotationActive, dd4hep::Position(xRotatedRead, yRotatedRead, 0))));
    activePhysVols.back().addPhysVolID("module", iPlane);
    activePhysVols.back().addPhysVolID("type", 0); // 0 = active, 1 = passive, 2 = readout
  }
  dd4hep::PlacedVolume bathPhysVol = envelopeVol.placeVolume(bathVol);
  bathDetElem.setPlacement(bathPhysVol);
  for (uint iPlane = 0; iPlane < numPlanes; iPlane++) {
    dd4hep::DetElement activeDetElem(bathDetElem, "active" + std::to_string(iPlane), iPlane);
    activeDetElem.setPlacement(activePhysVols[iPlane]);
    for (uint iLayer = 0; iLayer < numLayers; iLayer++) {
      dd4hep::DetElement layerDetElem(activeDetElem, "layer" + std::to_string(iLayer), iLayer);
      layerDetElem.setPlacement(layerPhysVols[iLayer]);
    }
  }

  // Place the envelope
  dd4hep::Volume motherVol = aLcdd.pickMotherVolume(caloDetElem);
  dd4hep::PlacedVolume envelopePhysVol = motherVol.placeVolume(envelopeVol);
  envelopePhysVol.addPhysVolID("system", xmlDetElem.id());
  caloDetElem.setPlacement(envelopePhysVol);

  // Create caloData object
  auto caloData = new dd4hep::rec::LayeredCalorimeterData;
  caloData->layoutType = dd4hep::rec::LayeredCalorimeterData::BarrelLayout;
  caloDetElem.addExtension<dd4hep::rec::LayeredCalorimeterData>(caloData);

  caloData->extent[0] = Rmin;
  caloData->extent[1] = Rmax; // or r_max ?
  caloData->extent[2] = 0.;   // NN: for barrel detectors this is 0
  caloData->extent[3] = caloDim.dz();

  // Set type flags
  dd4hep::xml::setDetectorTypeFlag(xmlDetElem, caloDetElem);
  dd4hep::rec::MaterialManager matMgr(envelopeVol);
  dd4hep::rec::LayeredCalorimeterData::Layer caloLayer;

  double rad_first = Rmin;
  double rad_last = 0;
  double scale_fact = dR / (-Rmin * cos(angle) + sqrt(pow(Rmax, 2) - pow(Rmin * sin(angle), 2)));
  // since the layer height is given along the electrode and not along the radius it needs to be scaled to get the
  // values of layer height radially
  std::cout << "Scaling factor " << scale_fact << std::endl;
  for (size_t il = 0; il < layerHeight.size(); il++) {
    double thickness_sen = 0.;
    double absorberThickness = 0.;

    rad_last = rad_first + (layerHeight[il] * scale_fact);
    dd4hep::rec::Vector3D ivr1 =
        dd4hep::rec::Vector3D(0., rad_first, 0); // defining starting vector points of the given layer
    dd4hep::rec::Vector3D ivr2 =
        dd4hep::rec::Vector3D(0., rad_last, 0); // defining end vector points of the given layer

    std::cout << "radius first " << rad_first << " radius last " << rad_last << std::endl;
    const dd4hep::rec::MaterialVec& materials =
        matMgr.materialsBetween(ivr1, ivr2); // calling material manager to get material info between two points
    auto mat = matMgr.createAveragedMaterial(materials); // creating average of all the material between two points to
                                                         // calculate X0 and lambda of averaged material
    const double nRadiationLengths = mat.radiationLength();
    const double nInteractionLengths = mat.interactionLength();
    const double difference_bet_r1r2 = (ivr1 - ivr2).r();
    const double value_of_x0 = layerHeight[il] / nRadiationLengths;
    const double value_of_lambda = layerHeight[il] / nInteractionLengths;
    std::string str1("LAr");

    for (size_t imat = 0; imat < materials.size(); imat++) {

      std::string str2(materials.at(imat).first.name());
      if (str1.compare(str2) == 0) {
        thickness_sen += materials.at(imat).second;
      } else {
        absorberThickness += materials.at(imat).second;
      }
    }
    rad_first = rad_last;
    std::cout << "The sensitive thickness is " << thickness_sen << std::endl;
    std::cout << "The absorber thickness is " << absorberThickness << std::endl;
    std::cout << "The radiation length is " << value_of_x0 << " and the interaction length is " << value_of_lambda
              << std::endl;

    caloLayer.distance = rad_first;
    caloLayer.sensitive_thickness = thickness_sen;
    caloLayer.inner_nRadiationLengths = value_of_x0 / 2.0;
    caloLayer.inner_nInteractionLengths = value_of_lambda / 2.0;
    caloLayer.inner_thickness = difference_bet_r1r2 / 2.0;

    caloLayer.outer_nRadiationLengths = value_of_x0 / 2.0;
    caloLayer.outer_nInteractionLengths = value_of_lambda / 2.0;
    caloLayer.outer_thickness = difference_bet_r1r2 / 2;

    caloLayer.absorberThickness = absorberThickness;
    caloLayer.cellSize0 = 20 * dd4hep::mm;
    caloLayer.cellSize1 = 20 * dd4hep::mm;

    caloData->layers.push_back(caloLayer);
  }
  return caloDetElem;
}
} // namespace det

DECLARE_DETELEMENT(ECalBarrel_NobleLiquid_InclinedTrapezoids_o1_v02, det::createECalBarrelInclined)
