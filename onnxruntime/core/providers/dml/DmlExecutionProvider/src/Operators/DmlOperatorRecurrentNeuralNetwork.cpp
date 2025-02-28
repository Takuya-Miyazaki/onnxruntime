// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "./precomp.h"

namespace Dml
{

// Base class for RNN ops (simple RNN, GRU, and LSTM).
//
class DmlOperatorRecurrentBase: public DmlOperator, public RecurrentHelper
{
public:
    using Self = DmlOperatorRecurrentBase;

    explicit DmlOperatorRecurrentBase(const MLOperatorKernelCreationContext& kernelInfo):
        DmlOperator(kernelInfo),
        RecurrentHelper(kernelInfo, kernelInfo.GetTensorShapeDescription())
    {
    }

    void Initialize( 
        const MLOperatorKernelCreationContext& kernelInfo,
        uint32_t sequenceLengthInputIndex,
        gsl::span<const std::string> defaultActivations,
        const std::optional<const std::vector<std::optional<uint32_t>>>& kernelInputIndices = std::nullopt,
        const std::optional<const std::vector<std::optional<uint32_t>>>& kernelOutputIndices = std::nullopt)
    {
        DmlOperator::Initialize(kernelInfo, kernelInputIndices, kernelOutputIndices);

        m_direction = GetRNNDirection(kernelInfo);
        InitActivationDescs(kernelInfo, /*inout*/ m_activationOpDescs, defaultActivations);

        bool hasOutput = false;

        for (const TensorDesc& desc : m_outputTensorDescs)
        {
            if (desc.IsValid())
            {
                hasOutput = true;
                break;
            }
        }
          
        if (!hasOutput)
        {
            ML_INVALID_ARGUMENT("At least one output should be requested.");
        }

        if (m_inputTensorDescs.size() > sequenceLengthInputIndex && m_inputTensorDescs[sequenceLengthInputIndex].IsValid())
        {
            m_inputTensorDescs[sequenceLengthInputIndex].ForceUnsignedDataType();
        }
    }

    DML_RECURRENT_NETWORK_DIRECTION GetRNNDirection(const MLOperatorKernelCreationContext& kernelInfo)
    {
        std::string direction = kernelInfo.GetOptionalAttribute<std::string>(AttrName::Direction, AttrValue::DirectionForward);

        if (direction == AttrValue::DirectionForward) { return DML_RECURRENT_NETWORK_DIRECTION_FORWARD; }
        if (direction == AttrValue::DirectionReverse) { return DML_RECURRENT_NETWORK_DIRECTION_BACKWARD; }
        if (direction == AttrValue::DirectionBidirectional) { return DML_RECURRENT_NETWORK_DIRECTION_BIDIRECTIONAL; }
         
        ML_INVALID_ARGUMENT("Unsupported direction"); // throws
        return DML_RECURRENT_NETWORK_DIRECTION_FORWARD;
    }

    void InitActivationDescs(const MLOperatorKernelCreationContext& kernelInfo, _Out_ std::vector<DML_OPERATOR_DESC>& descs, gsl::span<const std::string> defaultActivations)
    {
        std::vector<std::string> activations = kernelInfo.GetOptionalStringAttributeVector(AttrName::Activations);
        if (activations.empty())
        {
            uint32_t loopCount = (m_direction == DML_RECURRENT_NETWORK_DIRECTION_BIDIRECTIONAL) ? 2 : 1;
            // Default value is set if none are given
            for (uint32_t i = 0; i < loopCount; i++)
            {
                std::copy(defaultActivations.begin(), defaultActivations.end(), std::back_inserter(activations));
            }
        }

        // resize the array to the correct direction count. The schema defaults to always be 2 elements which is wrong for single direction case.
        activations.resize((m_direction == DML_RECURRENT_NETWORK_DIRECTION_BIDIRECTIONAL) ? 2 * defaultActivations.size() : defaultActivations.size());

        descs.resize(activations.size());
        m_activationDescs.resize(activations.size());

        // Some functions have additional parameters. It is assumed the alpha/beta values will 
        // be ordered by function, so this treats the respective operator attributes as stacks.
        std::vector<float> alphas; 
        if (kernelInfo.HasAttribute(AttrName::ActivationAlpha, MLOperatorAttributeType::FloatArray))
        {
            alphas = kernelInfo.GetAttributeVector<float>(AttrName::ActivationAlpha);
        }
        
        std::vector<float> betas;
        if (kernelInfo.HasAttribute(AttrName::ActivationBeta, MLOperatorAttributeType::FloatArray))
        {
            betas = kernelInfo.GetAttributeVector<float>(AttrName::ActivationBeta);
        }
        
        size_t currentAlpha = 0;
        size_t currentBeta = 0;

        auto NextAlpha = [&](DML_OPERATOR_TYPE function)
        {
            if (currentAlpha >= alphas.size())
            {
                return ActivationHelper::GetDefaultAlpha(function);
            }

            return alphas[currentAlpha++];
        };

        auto NextBeta = [&](DML_OPERATOR_TYPE function)
        {
            if (currentBeta >= betas.size())
            {
                return ActivationHelper::GetDefaultBeta(function);
            }

            return betas[currentBeta++];
        };

        for (size_t i = 0; i < activations.size(); ++i)
        {
            const std::string& activationName = activations[i];
            DML_OPERATOR_DESC& desc = descs[i];
            ActivationOperatorDescUnion& activationDesc = m_activationDescs[i];
            desc.Desc = &activationDesc;
         
            if (CompareActivationName(activationName, AttrValue::ActivationRelu))
            { 
                desc.Type = DML_OPERATOR_ACTIVATION_RELU;
            }  
            else if (CompareActivationName(activationName, AttrValue::ActivationLeakyRelu))
            { 
                desc.Type = DML_OPERATOR_ACTIVATION_LEAKY_RELU;
                activationDesc.leakyRelu.Alpha = NextAlpha(desc.Type);
            }
            else if (CompareActivationName(activationName, AttrValue::ActivationThresholdedRelu))
            { 
                desc.Type = DML_OPERATOR_ACTIVATION_THRESHOLDED_RELU;
                activationDesc.thresholdedRelu.Alpha = NextAlpha(desc.Type);
            }           
            else if (CompareActivationName(activationName, AttrValue::ActivationTanh))
            { 
                desc.Type = DML_OPERATOR_ACTIVATION_TANH;
            }           
            else if (CompareActivationName(activationName, AttrValue::ActivationScaledTanh))
            { 
                desc.Type = DML_OPERATOR_ACTIVATION_SCALED_TANH;
                activationDesc.scaledTanh.Alpha = NextAlpha(desc.Type);
                activationDesc.scaledTanh.Beta = NextBeta(desc.Type);
            }     
            else if (CompareActivationName(activationName, AttrValue::ActivationSigmoid))
            { 
                desc.Type = DML_OPERATOR_ACTIVATION_SIGMOID;
            }          
            else if (CompareActivationName(activationName, AttrValue::ActivationSigmoidHard))
            { 
                desc.Type = DML_OPERATOR_ACTIVATION_HARD_SIGMOID;
                activationDesc.hardSigmoid.Alpha = NextAlpha(desc.Type);
                activationDesc.hardSigmoid.Beta = NextBeta(desc.Type);
            }         
            else if (CompareActivationName(activationName, AttrValue::ActivationElu))
            { 
                desc.Type = DML_OPERATOR_ACTIVATION_ELU;
                activationDesc.elu.Alpha = NextAlpha(desc.Type);
            }          
            else if (CompareActivationName(activationName, AttrValue::ActivationSoftsign))
            { 
                desc.Type = DML_OPERATOR_ACTIVATION_SOFTSIGN;
            }         
            else if (CompareActivationName(activationName, AttrValue::ActivationSoftplus))
            { 
                desc.Type = DML_OPERATOR_ACTIVATION_SOFTPLUS;
            }
            else
            {
                ML_INVALID_ARGUMENT("Unsupported activation function");
            }
        }
    }
    
    bool CompareActivationName(std::string_view activationName, std::string_view attrValue)
    {
        auto comparer = [](char a, char b) {return std::tolower(a) == std::tolower(b);};
        return std::equal(activationName.begin(), activationName.end(), attrValue.begin(), attrValue.end(), comparer);
    }

    void Compute(const MLOperatorKernelContext& kernelContext) override
    {
        // Assume that enough GPU work has been queued up after the RNN operator that it is worth
        // kicking it off, to enable subsequent CPU work to be parallelized with this GPU work.
        DmlOperator::Compute(kernelContext);
        m_executionProvider->Flush();
    }

protected:
    std::vector<DML_OPERATOR_DESC> m_activationOpDescs;
    std::vector<ActivationOperatorDescUnion> m_activationDescs;

    DML_RECURRENT_NETWORK_DIRECTION m_direction;
};

// Simple RNN
// 
class DmlOperatorRecurrentNeuralNetwork : public DmlOperatorRecurrentBase
{
public:
    DmlOperatorRecurrentNeuralNetwork(const MLOperatorKernelCreationContext& kernelInfo)
    :   DmlOperatorRecurrentBase(kernelInfo)
    {
        // HiddenInit and SequenceLengths are reverse with ONNX ordering
        std::vector<std::optional<uint32_t>> kernelInputIndices = {0, 1, 2, 3, 5, 4};
        std::vector<std::optional<uint32_t>> kernelOutputIndices = {0, 1};

        std::array<std::string, 1> defaultActivations = {AttrValue::ActivationTanh};

        DmlOperatorRecurrentBase::Initialize(kernelInfo, IN_SEQUENCE_LENGTHS, defaultActivations, kernelInputIndices, kernelOutputIndices);

        std::vector<DML_TENSOR_DESC> inputDescs  = GetDmlInputDescs();
        std::vector<DML_TENSOR_DESC> outputDescs = GetDmlOutputDescs();

        DML_RNN_OPERATOR_DESC rnnDesc = {};
        
        rnnDesc.InputTensor             = &inputDescs[IN_X];
        rnnDesc.WeightTensor            = &inputDescs[IN_WEIGHTS];
        rnnDesc.RecurrenceTensor        = &inputDescs[IN_RECURRENCE];
        rnnDesc.BiasTensor              = (inputDescs[IN_BIAS].Desc             != nullptr) ? &inputDescs[IN_BIAS]              : nullptr;
        rnnDesc.HiddenInitTensor        = (inputDescs[IN_HIDDEN_INIT].Desc      != nullptr) ? &inputDescs[IN_HIDDEN_INIT]       : nullptr;
        rnnDesc.SequenceLengthsTensor   = (inputDescs[IN_SEQUENCE_LENGTHS].Desc != nullptr) ? &inputDescs[IN_SEQUENCE_LENGTHS]  : nullptr;
        rnnDesc.OutputSequenceTensor    = (outputDescs[OUT_SEQUENCE].Desc       != nullptr) ? &outputDescs[OUT_SEQUENCE]        : nullptr;
        rnnDesc.OutputSingleTensor      = (outputDescs[OUT_SINGLE].Desc         != nullptr) ? &outputDescs[OUT_SINGLE]          : nullptr;

        rnnDesc.ActivationDescCount = gsl::narrow_cast<uint32_t>(m_activationOpDescs.size());
        rnnDesc.ActivationDescs = m_activationOpDescs.data();

        rnnDesc.Direction = m_direction;
        DML_OPERATOR_DESC opDesc = { DML_OPERATOR_RNN, &rnnDesc };

        SetDmlOperatorDesc(opDesc, kernelInfo);
    }

private:
    // Inputs in DML's order, which is different from ONNX.
    enum InputTensors 
    { 
        IN_X, // X
        IN_WEIGHTS, // W
        IN_RECURRENCE, // R
        IN_BIAS, // B
        IN_HIDDEN_INIT, // initial_h
        IN_SEQUENCE_LENGTHS,  // sequence_lens
    };
        
    enum OutputTensors
    { 
        OUT_SEQUENCE, // Y
        OUT_SINGLE
    };
};

// GRU
// 
class DmlOperatorGatedRecurrentUnit : public DmlOperatorRecurrentBase
{
public:
    DmlOperatorGatedRecurrentUnit(const MLOperatorKernelCreationContext& kernelInfo)
    :   DmlOperatorRecurrentBase(kernelInfo)
    {
        std::array<std::string, 2> defaultActivations = {AttrValue::ActivationSigmoid, AttrValue::ActivationTanh};
        bool linearBeforeReset = kernelInfo.GetOptionalAttribute<int64_t>(AttrName::LinearBeforeReset, 0) != 0;

        // HiddenInit and SequenceLengths are reverse with ONNX ordering
        std::vector<std::optional<uint32_t>> kernelInputIndices = {0, 1, 2, 3, 5, 4};
        std::vector<std::optional<uint32_t>> kernelOutputIndices = {0, 1};

        DmlOperatorRecurrentBase::Initialize(kernelInfo, IN_SEQUENCE_LENGTHS, defaultActivations, kernelInputIndices, kernelOutputIndices);

        std::vector<DML_TENSOR_DESC> inputDescs  = GetDmlInputDescs();
        std::vector<DML_TENSOR_DESC> outputDescs = GetDmlOutputDescs();

        DML_GRU_OPERATOR_DESC rnnDesc = {};
        
        rnnDesc.InputTensor             = &inputDescs[IN_X];
        rnnDesc.WeightTensor            = &inputDescs[IN_WEIGHTS];
        rnnDesc.RecurrenceTensor        = &inputDescs[IN_RECURRENCE];
        rnnDesc.BiasTensor              = (inputDescs[IN_BIAS].Desc             != nullptr) ? &inputDescs[IN_BIAS]              : nullptr;
        rnnDesc.HiddenInitTensor        = (inputDescs[IN_HIDDEN_INIT].Desc      != nullptr) ? &inputDescs[IN_HIDDEN_INIT]       : nullptr;
        rnnDesc.SequenceLengthsTensor   = (inputDescs[IN_SEQUENCE_LENGTHS].Desc != nullptr) ? &inputDescs[IN_SEQUENCE_LENGTHS]  : nullptr;
        rnnDesc.OutputSequenceTensor    = (outputDescs[OUT_SEQUENCE].Desc       != nullptr) ? &outputDescs[OUT_SEQUENCE]        : nullptr;
        rnnDesc.OutputSingleTensor      = (outputDescs[OUT_SINGLE].Desc         != nullptr) ? &outputDescs[OUT_SINGLE]          : nullptr;

        rnnDesc.ActivationDescCount = gsl::narrow_cast<uint32_t>(m_activationOpDescs.size());
        rnnDesc.ActivationDescs = m_activationOpDescs.data();

        rnnDesc.Direction = m_direction;
        rnnDesc.LinearBeforeReset = linearBeforeReset ? TRUE : FALSE;
        DML_OPERATOR_DESC opDesc = { DML_OPERATOR_GRU, &rnnDesc };

        SetDmlOperatorDesc(opDesc, kernelInfo);
    }

private:
    // Inputs in DML's order, which is different from ONNX.
    enum InputTensors 
    { 
        IN_X,
        IN_WEIGHTS,
        IN_RECURRENCE,
        IN_BIAS,
        IN_HIDDEN_INIT,
        IN_SEQUENCE_LENGTHS,
    };
        
    enum OutputTensors
    { 
        OUT_SEQUENCE, // Y
        OUT_SINGLE
    };
};

// LSTM
// 
class DmlOperatorLongShortTermUnit : public DmlOperatorRecurrentBase
{
public:
    DmlOperatorLongShortTermUnit(const MLOperatorKernelCreationContext& kernelInfo)
    :   DmlOperatorRecurrentBase(kernelInfo)
    {
        std::array<std::string, 3> defaultActivations = {AttrValue::ActivationSigmoid, AttrValue::ActivationTanh, AttrValue::ActivationTanh};

        bool useClipThreshold = kernelInfo.HasAttribute(AttrName::Clip, MLOperatorAttributeType::Float);
        float clipThreshold = kernelInfo.GetOptionalAttribute<float>(AttrName::Clip, 0.0f);
        bool coupleInputForget = kernelInfo.GetOptionalAttribute<bool>(AttrName::InputForget, false);

        std::vector<std::optional<uint32_t>> kernelInputIndices = 
        {
            0, // DML Input tensor is ONNX input 0
            1, // DML Weight tensor is ONNX input 1 
            2, // DML Recurrence tensor is ONNX input 2 
            3, // DML Bias tensor is ONNX input 3
            5, // DML HiddenInit tensor is ONNX input 5
            6, // DML CellMem tensor is ONNX input 6
            4, // DML SequenceLengths tensor is ONNX input 4
            7  // DML Peephole tensor is ONNX input 7
        };

        std::vector<std::optional<uint32_t>> kernelOutputIndices = 
        {
            0, // DML OutputSequence tensor is ONNX input 0
            1, // DML OutputSingle tensor is ONNX input 1
            2, // DML OutputCellSingle tensor is ONNX input 2
        };

        DmlOperatorRecurrentBase::Initialize(kernelInfo, IN_SEQUENCE_LENGTHS, defaultActivations, kernelInputIndices, kernelOutputIndices);

        std::vector<DML_TENSOR_DESC> inputDescs  = GetDmlInputDescs();
        std::vector<DML_TENSOR_DESC> outputDescs = GetDmlOutputDescs();

        DML_LSTM_OPERATOR_DESC rnnDesc = {};
        
        rnnDesc.InputTensor             = &inputDescs[IN_X];
        rnnDesc.WeightTensor            = &inputDescs[IN_WEIGHTS];
        rnnDesc.RecurrenceTensor        = &inputDescs[IN_RECURRENCE];
        rnnDesc.BiasTensor              = (inputDescs[IN_BIAS].Desc             != nullptr) ? &inputDescs[IN_BIAS]              : nullptr;
        rnnDesc.HiddenInitTensor        = (inputDescs[IN_HIDDEN_INIT].Desc      != nullptr) ? &inputDescs[IN_HIDDEN_INIT]       : nullptr;
        rnnDesc.CellMemInitTensor       = (inputDescs[IN_CELL_GATE_INIT].Desc   != nullptr) ? &inputDescs[IN_CELL_GATE_INIT]    : nullptr;

        rnnDesc.SequenceLengthsTensor   = (inputDescs[IN_SEQUENCE_LENGTHS].Desc != nullptr) ? &inputDescs[IN_SEQUENCE_LENGTHS]  : nullptr;
        rnnDesc.PeepholeTensor          = (inputDescs[IN_PEEPHOLE].Desc         != nullptr) ? &inputDescs[IN_PEEPHOLE]  : nullptr;

        rnnDesc.OutputSequenceTensor    = (outputDescs[OUT_SEQUENCE].Desc       != nullptr) ? &outputDescs[OUT_SEQUENCE]        : nullptr;
        rnnDesc.OutputSingleTensor      = (outputDescs[OUT_SINGLE].Desc         != nullptr) ? &outputDescs[OUT_SINGLE]          : nullptr;
        rnnDesc.OutputCellSingleTensor  = (outputDescs[OUT_CELL_SINGLE].Desc    != nullptr) ? &outputDescs[OUT_CELL_SINGLE]     : nullptr;

        rnnDesc.ActivationDescCount = gsl::narrow_cast<uint32_t>(m_activationOpDescs.size());
        rnnDesc.ActivationDescs = m_activationOpDescs.data();

        rnnDesc.Direction = m_direction;
        rnnDesc.UseClipThreshold = useClipThreshold ? TRUE : FALSE;
        rnnDesc.ClipThreshold = clipThreshold;
        rnnDesc.CoupleInputForget = coupleInputForget ? TRUE : FALSE;

        DML_OPERATOR_DESC opDesc = { DML_OPERATOR_LSTM, &rnnDesc };

        SetDmlOperatorDesc(opDesc, kernelInfo);
    }

private:
    // Inputs in DML's order, which is different from ONNX.
    enum InputTensors 
    { 
        IN_X,
        IN_WEIGHTS,
        IN_RECURRENCE,
        IN_BIAS,
        IN_HIDDEN_INIT,
        IN_CELL_GATE_INIT,
        IN_SEQUENCE_LENGTHS,
        IN_PEEPHOLE
    };
        
    enum OutputTensors
    { 
        OUT_SEQUENCE, // Y
        OUT_SINGLE,
        OUT_CELL_SINGLE
    };
};

void CALLBACK QueryRecurrentNeuralNetwork(IMLOperatorSupportQueryContextPrivate* context, /*out*/ bool* isSupported)
{
    // layout=1 for batchwise operation is unsupported, added in opset 14 for RNN, GRU, and LSTM
    // (https://github.com/onnx/onnx/pull/3217, https://github.com/onnx/onnx/pull/2284).
    // Currently (2022-05-27) the ORT CPU execution provider (lstm_base.h) does not support it either,
    // with no models warranting it. When needed, it can be achieved with no new DML API's by just
    // swapping the size and strides in the TensorDesc before filling in the *_OPERATOR_DESC, where:
    //
    // layout=0: (default, consistent with opset 7)
    //      X.shape = [seq_length, batch_size, input_size]
    //      Y.shape = [seq_length, num_directions, batch_size, hidden_size]
    //      initial_h.shape = Y_h.shape = initial_c.shape = Y_c.shape = [num_directions, batch_size, hidden_size]
    // layout=1:
    //      X.shape = [batch_size, seq_length, input_size]
    //      Y.shape = [batch_size, seq_length, num_directions, hidden_size]
    //      initial_h.shape = Y_h.shape = initial_c.shape = Y_c.shape = [batch_size, num_directions, hidden_size]

    MLOperatorAttributes attributes(context);
    int32_t layout = attributes.GetOptionalAttribute<int32_t>(AttrName::Layout, 0);
    *isSupported = (layout == 0);
}

DML_OP_DEFINE_CREATION_FUNCTION(RNN,  DmlOperatorRecurrentNeuralNetwork);
DML_OP_DEFINE_CREATION_FUNCTION(GRU,  DmlOperatorGatedRecurrentUnit);
DML_OP_DEFINE_CREATION_FUNCTION(LSTM, DmlOperatorLongShortTermUnit);

} // namespace Dml
