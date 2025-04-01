#pragma once

#include "Config/Config.hpp"
#include "View/View.hpp"
#include "Model/Model.hpp"

class Controller {
public:
    Controller(const Config &cfg, Model &model, View &view);
    void run();
};
