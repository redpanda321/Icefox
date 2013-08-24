#include "Layers.h"
#include "LayerManagerOGL.h"
#include "BasicLayers.h"
#include "ImageLayers.h"
#ifdef MOZ_ENABLE_D3D9_LAYER
#include "LayerManagerD3D9.h"
#endif

using namespace mozilla;
using namespace layers;

void XXXNeverCalled_Layers()
{
  BasicLayerManager(nsnull);
  LayerManagerOGL(nsnull);
#ifdef MOZ_ENABLE_D3D9_LAYER
  LayerManagerD3D9(nsnull);
#endif
}
