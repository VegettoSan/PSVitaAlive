#include "installer/queue_coordinator.hpp"
#include "installer/install_controller.hpp"
#include "catalog/catalog_manager.hpp"

// Compile the complete coordinator against the actual Vita adapter, even
// before the catalog's admissions are switched over. Keep the old UI path
// until the main-loop routing, storage exclusions and presentation are ready.
template class psvitaalive::queue::Coordinator<psvitaalive::InstallController,
                                               psvitaalive::CatalogManager>;
