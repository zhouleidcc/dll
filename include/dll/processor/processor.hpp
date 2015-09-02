//=======================================================================
// Copyright (c) 2014-2015 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#pragma once

/*!
 * \file processor.hpp
 * \brief This file is made to be included by the dllp generated file only.
 */

#include <string>
#include <iostream>
#include <iomanip>

#include "dll/dbn.hpp"

#include "mnist/mnist_reader.hpp"
#include "mnist/mnist_utils.hpp"

namespace dll {

namespace processor {

constexpr const double stupid_default = -666.0;

struct datasource {
    std::string source_file;
    std::string reader;

    bool binarize = false;
    bool normalize = false;

    long limit = -1;

    datasource(){}
    datasource(std::string source_file, std::string reader) : source_file(source_file), reader(reader) {}

    bool empty() const {
        return source_file.empty();
    }
};

struct datasource_pack {
    datasource samples;
    datasource labels;
};

struct pretraining_desc {
    std::size_t epochs = 25;
};

struct training_desc {
    std::size_t epochs = 25;
    double learning_rate = stupid_default;
    double momentum = stupid_default;
    std::size_t batch_size = 0;
};

struct weights_desc {
    std::string file = "weights.dat";
};

struct task {
    dll::processor::datasource_pack pretraining;
    dll::processor::datasource_pack training;
    dll::processor::datasource_pack testing;

    dll::processor::pretraining_desc pt_desc;
    dll::processor::training_desc ft_desc;
    dll::processor::weights_desc w_desc;
};

template<typename Sample>
bool read_samples(const datasource& ds, std::vector<Sample>& samples){
    if(ds.reader == "mnist"){
        std::size_t limit = 0;

        if(ds.limit > 0){
            limit = ds.limit;
        }

        mnist::read_mnist_image_file<std::vector, Sample>(samples, ds.source_file, limit, []{ return Sample(1 * 28 * 28); });

        if(ds.binarize){
            mnist::binarize_each(samples);
        }

        if(ds.normalize){
            mnist::normalize_each(samples);
        }

        return !samples.empty();
    } else {
        std::cout << "dllp: error: unknown samples reader: " << ds.reader << std::endl;
        return false;
    }
}

template<typename Label>
bool read_labels(const datasource& ds, std::vector<Label>& labels){
    if(ds.reader == "mnist"){
        std::size_t limit = 0;

        if(ds.limit > 0){
            limit = ds.limit;
        }

        mnist::read_mnist_label_file<std::vector, Label>(labels, ds.source_file, limit);

        return !labels.empty();
    } else {
        std::cout << "dllp: error: unknown labels reader: " << ds.reader << std::endl;
        return false;
    }
}

inline void print_title(const std::string& value){
    std::cout << std::string(25, ' ') << std::endl;
    std::cout << std::string(25, '*') << std::endl;
    std::cout << "* " << value << std::string(25 - value.size() - 3, ' ') << "*" << std::endl;
    std::cout << std::string(25, '*') << std::endl;
    std::cout << std::string(25, ' ') << std::endl;
}

template<typename DBN>
void execute(DBN& dbn, task& task, const std::vector<std::string>& actions){
    print_title("Network");
    dbn.display();

    using dbn_t = std::decay_t<DBN>;

    //Execute all the actions sequentially
    for(auto& action : actions){
        if(action == "pretrain"){
            print_title("Pretraining");

            if(task.pretraining.samples.empty()){
                std::cout << "dllp: error: pretrain is not possible without a pretraining input" << std::endl;
                return;
            }

            std::vector<typename dbn_t::input_t> pt_samples;

            //Try to read the samples
            if(!read_samples(task.pretraining.samples, pt_samples)){
                std::cout << "dllp: error: failed to read the pretraining samples" << std::endl;
                return;
            }

            //Pretrain the network
            dbn.pretrain(pt_samples.begin(), pt_samples.end(), task.pt_desc.epochs);
        } else if(action == "train"){
            print_title("Training");

            if(task.training.samples.empty() || task.training.labels.empty()){
                std::cout << "dllp: error: train is not possible without samples and labels" << std::endl;
                return;
            }

            std::vector<typename dbn_t::input_t> ft_samples;
            std::vector<std::size_t> ft_labels;

            //Try to read the samples
            if(!read_samples(task.training.samples, ft_samples)){
                std::cout << "dllp: error: failed to read the training samples" << std::endl;
                return;
            }

            //Try to read the labels
            if(!read_labels(task.training.labels, ft_labels)){
                std::cout << "dllp: error: failed to read the training labels" << std::endl;
                return;
            }

            //Train the network
            dbn.fine_tune(ft_samples, ft_labels, task.ft_desc.epochs);
        } else if(action == "test"){
            print_title("Testing");

            if(task.testing.samples.empty() || task.testing.labels.empty()){
                std::cout << "dllp: error: test is not possible without samples and labels" << std::endl;
                return;
            }

            std::vector<typename dbn_t::input_t> test_samples;
            std::vector<std::size_t> test_labels;

            //Try to read the samples
            if(!read_samples(task.testing.samples, test_samples)){
                std::cout << "dllp: error: failed to read the training samples" << std::endl;
                return;
            }

            //Try to read the labels
            if(!read_labels(task.testing.labels, test_labels)){
                std::cout << "dllp: error: failed to read the training labels" << std::endl;
                return;
            }

            auto classes = dbn_t::output_size();

            etl::dyn_matrix<std::size_t, 2> conf(classes, classes, 0.0);

            std::size_t n = test_samples.size();
            std::size_t tp = 0;

            for(std::size_t i = 0; i < test_samples.size(); ++i){
                auto sample = test_samples[i];
                auto label = test_labels[i];

                auto predicted = dbn.predict(sample);

                if(predicted == label){
                    ++tp;
                }

                ++conf(label, predicted);
            }

            double test_error = (n - tp) / double(n);

            std::cout << "Error rate: " << test_error << std::endl;
            std::cout << "Accuracy: " << (1.0 - test_error) << std::endl << std::endl;

            std::cout << "Results per class" << std::endl;

            double overall = 0.0;

            std::cout << "   | Accuracy | Error rate |" << std::endl;

            for(std::size_t l = 0; l < classes; ++l){
                std::size_t total = etl::sum(conf(l));
                double acc = (total - conf(l, l)) / double(total);
                std::cout << std::setw(3) << l;
                std::cout << "|" << std::setw(10) << (1.0 - acc) << "|" << std::setw(12) << acc << "|" << std::endl;
                overall += acc;
            }

            std::cout << std::endl;

            std::cout << "Overall Error rate: " << overall / classes << std::endl;
            std::cout << "Overall Accuracy: " << 1.0 - (overall / classes) << std::endl << std::endl;

            std::cout << "Confusion Matrix (%)" << std::endl << std::endl;

            std::cout << "    ";
            for(std::size_t l = 0; l < classes; ++l){
                std::cout << std::setw(5) << l << " ";
            }
            std::cout << std::endl;

            for(std::size_t l = 0; l < classes; ++l){
                std::size_t total = etl::sum(conf(l));
                std::cout << std::setw(3) << l << "|";
                for(std::size_t p = 0; p < classes; ++p){
                    std::cout << std::setw(5) << std::setprecision(2) << 100.0 * (conf(l,p) / double(total)) << "|";
                }
                std::cout << std::endl;
            }
            std::cout << std::endl;
        } else if(action == "save"){
            print_title("Save Weights");

            dbn.store(task.w_desc.file);
            std::cout << "Weights saved" << std::endl;
        } else if(action == "load"){
            print_title("Load Weights");

            dbn.load(task.w_desc.file);
            std::cout << "Weights loaded" << std::endl;
        } else {
            std::cout << "dllp: error: Invalid action: " << action << std::endl;
        }
    }


    //TODO
}

} //end of namespace processor

} //end of namespace dll
