import React from 'react';
import ReactDOM from 'react-dom/client';
import './index.css';
import reportWebVitals from './reportWebVitals';


function Benchmark() {

  const startBenchmark = () => {
    axios.get("benchmark")
      .then( (response) => {
        console.log("benchmark response: ", response);
      }
  };

  return (
    <div className="Benchmark" >
      <div>
        <button onClick={startBenchmark}> start benchmark </button>
      </div>
    </div>
  );
}

export default Benchmark;
