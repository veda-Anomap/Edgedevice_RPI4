#ifndef TESTER_REGISTRY_H
#define TESTER_REGISTRY_H

#include <vector>
#include <string>
#include <memory>
#include "../src/imageprocessing/IImageEnhancer.h"
#include "../src/imageprocessing/AdvancedEnhancers.h"
#include "../src/imageprocessing/LowLightEnhancer.h"

struct EnhancerInfo {
    int id;
    std::string name;
    std::shared_ptr<IImageEnhancer> enhancer;
};

class TesterRegistry {
public:
    static std::vector<EnhancerInfo> getAllEnhancers() {
        std::vector<EnhancerInfo> list;
        list.push_back({1, "1. RETINEX",      std::make_shared<RetinexEnhancer>()});
        list.push_back({2, "2. YUV ADV",      std::make_shared<YuvAdvancedEnhancer>()});
        list.push_back({3, "3. WWGIF",        std::make_shared<WWGIFEnhancer>()});
        list.push_back({4, "4. TONEMAP",      std::make_shared<ToneMappingEnhancer>()});
        list.push_back({5, "5. DETAIL",       std::make_shared<DetailBoostEnhancer>()});
        list.push_back({6, "6. HYBRID",       std::make_shared<HybridEnhancer>()});
        list.push_back({7, "7. BALANCED_V5",  std::make_shared<UltimateBalancedEnhancer>()});
        list.push_back({8, "8. CleanSharp",   std::make_shared<CleanSharpEnhancer>()});
        list.push_back({9, "9. RAW ORIGIN",   nullptr}); // No-op for Raw
        return list;
    }
};

#endif // TESTER_REGISTRY_H
