# LIGTAS Model

## Description
LIGTAS is a machine learning model for voltage monitoring and safety classification. The project includes a trained Random Forest model that classifies voltage readings into three categories: Safe, Check, and Dangerous. It features both Python training/validation scripts and C++ headers for embedded deployment.

## Dependencies

### Python Dependencies
The following Python packages are required:

- `pandas` - Data manipulation and analysis
- `numpy` - Numerical computing
- `matplotlib` - Plotting and visualization
- `seaborn` - Statistical data visualization
- `scikit-learn` (sklearn) - Machine learning algorithms
- `joblib` - Model serialization
- `openpyxl` - Excel file handling

### C++ Dependencies
- Standard C++ libraries
- Arduino framework (for embedded deployment, includes String class)

## Installation

### Python Environment Setup
1. Ensure Python 3.7+ is installed
2. Install required packages:
   ```bash
   pip install pandas numpy matplotlib seaborn scikit-learn joblib openpyxl
   ```

### For Jupyter Notebook
If you plan to run the training notebook:
```bash
pip install jupyter
```

### C++ Setup
- Use an Arduino-compatible IDE (e.g., Arduino IDE, PlatformIO)
- Include the header files `ligtas_ml.h` and `ligtas_model.h` in your project

## Usage

### Training and Validation
1. Open `ligtas_training.ipynb` in Jupyter Notebook
2. Run the cells sequentially to train and validate the model
3. The notebook includes data loading, preprocessing, model training, and evaluation

### Response Time Chart Generation
1. Prepare the Excel template `ligtas_response_time_template.xlsx`
2. Run the Python script:
   ```bash
   python ligtas_response_time_chart.py
   ```
3. The script will generate `response_time_chart.png`

### C++ Integration
- Include `ligtas_ml.h` in your Arduino sketch
- Use the provided scaler values and model for inference on embedded devices

## Files
- `ligtas_ml.h` - C++ ML wrapper header
- `ligtas_model.h` - Model definitions
- `ligtas_training.ipynb` - Jupyter notebook for training
- `ligtas_response_time_chart.py` - Response time chart generator
- `ligtas_voltage_dataset.csv` - Training dataset

## Model Details
- Algorithm: Random Forest (100 trees)
- Classes: 0=Safe, 1=Check, 2=Dangerous
- Features: 8 engineered features from voltage readings
- Accuracy: 99.96%